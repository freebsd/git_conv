/*
 *  Copyright (C) 2007  Thiago Macieira <thiago@kde.org>
 *  Copyright (C) 2009 Thomas Zander <zander@kde.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "repository.h"
#include "CommandLineParser.h"
#include <QTextStream>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLinkedList>
#include <QRegularExpression>

static const int maxSimultaneousProcesses = 100;

typedef unsigned long long mark_t;
static const mark_t initialMark = 42000000;
static const mark_t maxMark = ULONG_MAX;

class FastImportRepository : public Repository
{
public:
    struct AnnotatedTag
    {
        QString supportingRef;
        QByteArray svnprefix;
        QByteArray author;
        QByteArray log;
        uint dt;
        int revnum;
    };
    class Transaction : public Repository::Transaction
    {
        Q_DISABLE_COPY(Transaction)
        friend class FastImportRepository;

        FastImportRepository *repository;
        QByteArray branch;
        QByteArray svnprefix;
        QByteArray author;
        QByteArray log;
        uint datetime;
        int revnum;

        // Not a plain map to preserve order of insertion to retain backwards
        // compatibility. this should then result in parents being listed in
        // the order of the svn log -v output, not sorted by from-revision or
        // from-branch.
        QMap<QString, int> merge_map;
        QVector<int> merges;

        QStringList deletedFiles;
        QList<QPair<QString, QString>> renamedFiles;
        QByteArray modifiedFiles;
        QByteArray resetFromTree;

        inline Transaction() {}
    public:
        ~Transaction();
        int commit();

        void setAuthor(const QByteArray &author);
        void setDateTime(uint dt);
        void setLog(const QByteArray &log);

        void noteCopyFromBranch(const QString &branchFrom, int branchRevNum, bool allow_heuristic=true);

        void deleteFile(const QString &path);
        void renameFile(const QString &from, const QString &to);
        QIODevice *addFile(const QString &path, int mode, qint64 length);

        bool commitNote(const QByteArray &noteText, bool append,
                        const QByteArray &commit = QByteArray());
        const QByteArray& getBranch() const { return branch; }
    };
    FastImportRepository(const Rules::Repository &rule);
    int setupIncremental(int &cutoff);
    void restoreAnnotatedTags();
    void restoreBranchNotes();
    void restoreLog();
    ~FastImportRepository();

    void reloadBranches();
    int createBranch(const QString &branch, int revnum,
                     const QString &branchFrom, int branchRevNum);
    int createBranch(const QString &branch, int revnum,
                     const QString &tree_hash, Repository::Transaction* txn);
    int createBranch(const QString &branch, int revnum,
                     const QString &branchFrom, int branchRevNum,
                     const QString &tree_hash, Repository::Transaction* txn);
    int deleteBranch(const QString &branch, int revnum);
    Repository::Transaction *newTransaction(const QString &branch, const QString &svnprefix, int revnum);

    void createAnnotatedTag(const QString &ref, const QString &svnprefix, int revnum,
                            const QByteArray &author, uint dt,
                            const QByteArray &log);
    void finalizeTags();
    void saveBranchNotes();
    void commit();

    bool branchExists(const QString& branch) const;
    const QByteArray branchNote(const QString& branch) const;
    void setBranchNote(const QString& branch, const QByteArray& noteText);

    bool hasPrefix() const;

    QString getName() const;
    Repository *getEffectiveRepository();
private:
    struct Branch
    {
        int created;
        QVector<int> commits;
        QVector<int> marks;
    };

    QHash<QString, Branch> branches;
    QHash<QString, QByteArray> branchNotes;
    QHash<QString, AnnotatedTag> annotatedTags;
    std::vector<std::pair<uint, std::unique_ptr<QByteArray>>> delayed_notes;
    QString name;
    QString prefix;
    LoggingQProcess fastImport;
    int commitCount;
    int outstandingTransactions;
    QByteArray deletedBranches;
    QByteArray resetBranches;
    QSet<QString> deletedBranchNames;
    QSet<QString> resetBranchNames;

  /* Optional filter to fix up log messages */
    QProcess filterMsg;
    QByteArray msgFilter(const QByteArray& msg);

    /* starts at 0, and counts up.  */
    mark_t last_commit_mark;

    /* starts at maxMark - 1 and counts down. Reset after each SVN revision */
    mark_t next_file_mark;

    bool processHasStarted;

    void startFastImport();
    void closeFastImport();

    // called when a transaction is deleted
    void forgetTransaction(Transaction *t);

    int resetBranch(const QString &branch, int revnum, mark_t mark, const QByteArray &resetTo, const QByteArray &comment);
    long long markFrom(const QString &branchFrom, int branchRevNum, QByteArray &desc);

    friend class ProcessCache;
    Q_DISABLE_COPY(FastImportRepository)
};

class ForwardingRepository : public Repository
{
    QString name;
    Repository *repo;
    QString prefix;
public:
    class Transaction : public Repository::Transaction
    {
        Q_DISABLE_COPY(Transaction)

        Repository::Transaction *txn;
        QString prefix;
    public:
        Transaction(Repository::Transaction *t, const QString &p) : txn(t), prefix(p) {}
        ~Transaction() { delete txn; }
        int commit() { return txn->commit(); }

        void setAuthor(const QByteArray &author) { txn->setAuthor(author); }
        void setDateTime(uint dt) { txn->setDateTime(dt); }
        void setLog(const QByteArray &log) { txn->setLog(log); }

        void noteCopyFromBranch (const QString &prevbranch, int revFrom, bool allow_heuristic=true)
        { txn->noteCopyFromBranch(prevbranch, revFrom, allow_heuristic); }

        void deleteFile(const QString &path) { txn->deleteFile(prefix + path); }
        void renameFile(const QString &from, const QString &to) { txn->renameFile(from, to); };
        QIODevice *addFile(const QString &path, int mode, qint64 length)
        { return txn->addFile(prefix + path, mode, length); }

        bool commitNote(const QByteArray &noteText, bool append,
                        const QByteArray &commit)
        { return txn->commitNote(noteText, append, commit); }

        const QByteArray& getBranch() const { return txn->getBranch(); }
    };

    ForwardingRepository(const QString &n, Repository *r, const QString &p) : name(n), repo(r), prefix(p) {}

    int setupIncremental(int &cutoff) { return 1; }
    void restoreAnnotatedTags() {}
    void restoreBranchNotes() {}
    void restoreLog() {}

    void reloadBranches() { return repo->reloadBranches(); }
    int createBranch(const QString &branch, int revnum,
                     const QString &branchFrom, int revFrom)
    { return repo->createBranch(branch, revnum, branchFrom, revFrom); }

    int createBranch(const QString &branch, int revnum,
                     const QString &tree_hash, Repository::Transaction* txn)
    { return repo->createBranch(branch, revnum, tree_hash, txn); }

    int createBranch(const QString &branch, int revnum,
                     const QString &branchFrom, int revFrom,
                     const QString &tree_hash, Repository::Transaction* txn)
    { return repo->createBranch(branch, revnum, branchFrom, revFrom, tree_hash, txn); }

    int deleteBranch(const QString &branch, int revnum)
    { return repo->deleteBranch(branch, revnum); }

    Repository::Transaction *newTransaction(const QString &branch, const QString &svnprefix, int revnum)
    {
        Repository::Transaction *t = repo->newTransaction(branch, svnprefix, revnum);
        return new Transaction(t, prefix);
    }

    void createAnnotatedTag(const QString &name, const QString &svnprefix, int revnum,
                            const QByteArray &author, uint dt,
                            const QByteArray &log)
    { repo->createAnnotatedTag(name, svnprefix, revnum, author, dt, log); }
    void finalizeTags() { /* loop that called this will invoke it on 'repo' too */ }
    void saveBranchNotes() { /* loop that called this will invoke it on 'repo' too */ }
    void commit() { repo->commit(); }

    bool branchExists(const QString& branch) const
    { return repo->branchExists(branch); }
    const QByteArray branchNote(const QString& branch) const
    { return repo->branchNote(branch); }
    void setBranchNote(const QString& branch, const QByteArray& noteText)
    { repo->setBranchNote(branch, noteText); }

    bool hasPrefix() const
    { return !prefix.isEmpty() || repo->hasPrefix(); }

    QString getName() const
    { return name; }
    Repository *getEffectiveRepository()
    { return repo->getEffectiveRepository(); }
};

class ProcessCache: QLinkedList<FastImportRepository *>
{
public:
    void touch(FastImportRepository *repo)
    {
        remove(repo);

        // if the cache is too big, remove from the front
        while (size() >= maxSimultaneousProcesses)
            takeFirst()->closeFastImport();

        // append to the end
        append(repo);
    }

    inline void remove(FastImportRepository *repo)
    {
#if QT_VERSION >= 0x040400
        removeOne(repo);
#else
        removeAll(repo);
#endif
    }
};
static ProcessCache processCache;

QDataStream &operator<<(QDataStream &out, const FastImportRepository::AnnotatedTag &annotatedTag)
{
    out << annotatedTag.supportingRef
        << annotatedTag.svnprefix
        << annotatedTag.author
        << annotatedTag.log
        << (quint64) annotatedTag.dt
        << (qint64) annotatedTag.revnum;
    return out;
}

QDataStream &operator>>(QDataStream &in, FastImportRepository::AnnotatedTag &annotatedTag)
{
    quint64 dt;
    qint64 revnum;

    in >> annotatedTag.supportingRef
       >> annotatedTag.svnprefix
       >> annotatedTag.author
       >> annotatedTag.log
       >> dt
       >> revnum;
    annotatedTag.dt = (uint) dt;
    annotatedTag.revnum = (int) revnum;
    return in;
}

Repository *createRepository(const Rules::Repository &rule, const QHash<QString, Repository *> &repositories)
{
    if (rule.forwardTo.isEmpty())
        return new FastImportRepository(rule);
    Repository *r = repositories[rule.forwardTo];
    if (!r) {
        qCritical() << "no repository with name" << rule.forwardTo << "found at" << rule.info();
        return r;
    }
    return new ForwardingRepository(rule.name, r, rule.prefix);
}

static QString marksFileName(QString name)
{
    name.replace('/', '_');
    name.prepend("marks-");
    return name;
}

static QString annotatedTagsFileName(QString name)
{
    name.replace('/', '_');
    name.prepend("annotatedTags-");
    return name;
}

static QString branchNotesFileName(QString name)
{
    name.replace('/', '_');
    name.prepend("branchNotes-");
    return name;
}

FastImportRepository::FastImportRepository(const Rules::Repository &rule)
    : name(rule.name), prefix(rule.forwardTo), fastImport(name), commitCount(0), outstandingTransactions(0),
      last_commit_mark(initialMark), next_file_mark(maxMark - 1), processHasStarted(false)
{
    foreach (Rules::Repository::Branch branchRule, rule.branches) {
        Branch branch;
        branch.created = 1;

        branches.insert(branchRule.name, branch);
    }

    // create the default branch
    branches["master"].created = 1;

    if (!CommandLineParser::instance()->contains("dry-run") && !CommandLineParser::instance()->contains("create-dump")) {
        fastImport.setWorkingDirectory(name);
        if (!QDir(name).exists()) { // repo doesn't exist yet.
            qDebug() << "Creating new repository" << name;
            QDir::current().mkpath(name);
            QProcess init;
            init.setWorkingDirectory(name);
            init.start("git", QStringList() << "--bare" << "init");
            init.waitForFinished(-1);
            QProcess casesensitive;
            casesensitive.setWorkingDirectory(name);
            casesensitive.start("git", QStringList() << "config" << "core.ignorecase" << "false");
            casesensitive.waitForFinished(-1);
            // Write description
            if (!rule.description.isEmpty()) {
                QFile fDesc(QDir(name).filePath("description"));
                if (fDesc.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                            fDesc.write(rule.description.toUtf8());
                    fDesc.putChar('\n');
                    fDesc.close();
                }
            }
            {
                QFile marks(name + "/" + marksFileName(name));
                marks.open(QIODevice::WriteOnly);
                marks.close();
            }
        }
    }
}

static QString logFileName(QString name)
{
    name.replace('/', '_');
    if (CommandLineParser::instance()->contains("create-dump"))
        name.append(".fi");
    else
        name.prepend("log-");
    return name;
}

static mark_t lastValidMark(const QString& name)
{
    QFile marksfile(name + "/" + marksFileName(name));
    if (!marksfile.open(QIODevice::ReadOnly))
        return 0;

    qDebug()  << "marksfile " << marksfile.fileName() ;
    mark_t prev_mark = initialMark;

    int lineno = 0;
    while (!marksfile.atEnd()) {
        QString line = marksfile.readLine();
        ++lineno;
        if (line.isEmpty())
            continue;

        mark_t mark = 0;
        if (line[0] == ':') {
            int sp = line.indexOf(' ');
            if (sp != -1) {
                QString m = line.mid(1, sp-1);
                mark = m.toULongLong();
            }
        }

        if (!mark) {
            qCritical() << marksfile.fileName() << "line" << lineno << "marks file corrupt?" << "mark " << mark;
            return 0;
        }

        if (mark == prev_mark) {
            qCritical() << marksfile.fileName() << "line" << lineno << "marks file has duplicates";
            return 0;
        }

        if (mark < prev_mark) {
            qCritical() << marksfile.fileName() << "line" << lineno << "marks file not sorted";
            return 0;
        }

        if (mark > prev_mark + 1)
            break;

        prev_mark = mark;
    }

    return prev_mark;
}

int FastImportRepository::setupIncremental(int &cutoff)
{
    QFile logfile(logFileName(name));
    if (!logfile.exists())
        return 1;

    logfile.open(QIODevice::ReadWrite);

    QRegExp progress("progress SVN r(\\d+) branch (.*) = :(\\d+)");

    mark_t last_valid_mark = lastValidMark(name);

    int last_revnum = 0;
    qint64 pos = 0;
    int retval = 0;
    QString bkup = logfile.fileName() + ".old";

    while (!logfile.atEnd()) {
        pos = logfile.pos();
        QByteArray line = logfile.readLine();
        int hash = line.indexOf('#');
        if (hash != -1)
            line.truncate(hash);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!progress.exactMatch(line))
            continue;

        int revnum = progress.cap(1).toInt();
        QString branch = progress.cap(2);
        mark_t mark = progress.cap(3).toULongLong();

        if (revnum >= cutoff)
            goto beyond_cutoff;

        if (revnum < last_revnum)
            qWarning() << "WARN:" << name << "revision numbers are not monotonic: "
                       << "got" << QString::number(last_revnum)
                       << "and then" << QString::number(revnum);

        if (mark > last_valid_mark) {
            qWarning() << "WARN:" << name << "unknown commit mark found: rewinding -- did you hit Ctrl-C?";
            cutoff = revnum;
            goto beyond_cutoff;
        }

        last_revnum = revnum;

        if (last_commit_mark < mark)
            last_commit_mark = mark;

        Branch &br = branches[branch];
        if (!br.created || !mark || br.marks.isEmpty() || !br.marks.last())
            br.created = revnum;
        br.commits.append(revnum);
        br.marks.append(mark);
    }

    retval = last_revnum + 1;
    if (retval == cutoff)
        /*
         * If a stale backup file exists already, remove it, so that
         * we don't confuse ourselves in 'restoreLog()'
         */
        QFile::remove(bkup);

    return retval;

  beyond_cutoff:
    // backup file, since we'll truncate
    QFile::remove(bkup);
    logfile.copy(bkup);

    // truncate, so that we ignore the rest of the revisions
    qDebug() << name << "truncating history to revision" << cutoff;
    logfile.resize(pos);
    return cutoff;
}

void FastImportRepository::restoreAnnotatedTags()
{
    QFile annotatedTagsFile(name + "/" + annotatedTagsFileName(name));
    if (!annotatedTagsFile.exists())
        return;
    annotatedTagsFile.open(QIODevice::ReadOnly);
    QDataStream annotatedTagsStream(&annotatedTagsFile);
    annotatedTagsStream >> annotatedTags;
    annotatedTagsFile.close();
}

void FastImportRepository::restoreBranchNotes()
{
    QFile branchNotesFile(name + "/" + branchNotesFileName(name));
    if (!branchNotesFile.exists())
        return;
    branchNotesFile.open(QIODevice::ReadOnly);
    QDataStream branchNotesStream(&branchNotesFile);
    branchNotesStream >> branchNotes;
    branchNotesFile.close();
}

void FastImportRepository::restoreLog()
{
    QString file = logFileName(name);
    QString bkup = file + ".old";
    if (!QFile::exists(bkup))
        return;
    QFile::remove(file);
    QFile::rename(bkup, file);
}

FastImportRepository::~FastImportRepository()
{
    Q_ASSERT(outstandingTransactions == 0);
    closeFastImport();
}

void FastImportRepository::closeFastImport()
{
    if (fastImport.state() != QProcess::NotRunning) {
        int fastImportTimeout = CommandLineParser::instance()->optionArgument(QLatin1String("fast-import-timeout"), QLatin1String("30")).toInt();
        if(fastImportTimeout == 0) {
            qDebug() << "Waiting forever for fast-import to finish.";
            fastImportTimeout = -1;
        } else {
            qDebug() << "Waiting" << fastImportTimeout << "seconds for fast-import to finish.";
            fastImportTimeout *= 10000;
        }
        fastImport.write("checkpoint\n");
        fastImport.waitForBytesWritten(-1);
        fastImport.closeWriteChannel();
        if (!fastImport.waitForFinished(fastImportTimeout)) {
            fastImport.terminate();
            if (!fastImport.waitForFinished(200))
                qWarning() << "WARN: git-fast-import for repository" << name << "did not die";
        }
    }
    processHasStarted = false;
    processCache.remove(this);
}

void FastImportRepository::reloadBranches()
{
    bool reset_notes = false;
    foreach (QString branch, branches.keys()) {
        Branch &br = branches[branch];

        if (br.marks.isEmpty() || !br.marks.last())
            continue;

        reset_notes = true;

        QByteArray branchRef = branch.toUtf8();
        if (!branchRef.startsWith("refs/"))
            branchRef.prepend("refs/heads/");

        startFastImport();
        fastImport.write("reset " + branchRef +
                        "\nfrom :" + QByteArray::number(br.marks.last()) + "\n\n"
                        "progress Branch " + branchRef + " reloaded\n");
    }

    if (reset_notes &&
        CommandLineParser::instance()->contains("add-metadata-notes")) {

        startFastImport();
        fastImport.write("reset refs/notes/commits\nfrom :" +
                         QByteArray::number(maxMark) +
                         "\n");
    }
}

long long FastImportRepository::markFrom(const QString &branchFrom, int branchRevNum, QByteArray &branchFromDesc)
{
    // Avoid using operator[], which creates a new branch for branchFrom that
    // might not even exist.
    if (!branches.contains(branchFrom)) {
        return -1;
    }
    Branch &brFrom = branches[branchFrom];
    if (!brFrom.created)
        return -1;

    if (brFrom.commits.isEmpty()) {
        return -1;
    }
    if (branchRevNum == brFrom.commits.last()) {
        return brFrom.marks.last();
    }

    QVector<int>::const_iterator it = qUpperBound(brFrom.commits, branchRevNum);
    if (it == brFrom.commits.begin()) {
        return 0;
    }

    int closestCommit = *--it;

    if (!branchFromDesc.isEmpty()) {
        branchFromDesc += " at r" + QByteArray::number(branchRevNum);
        if (closestCommit != branchRevNum) {
            branchFromDesc += " => r" + QByteArray::number(closestCommit);
        }
    }

    return brFrom.marks[it - brFrom.commits.begin()];
}

int FastImportRepository::createBranch(const QString &branch, int revnum,
                                       const QString &branchFrom, int branchRevNum)
{
    QByteArray branchFromDesc = "from branch " + branchFrom.toUtf8();
    long long mark = markFrom(branchFrom, branchRevNum, branchFromDesc);

    if (mark == -1) {
        qCritical() << branch << "in repository" << name
                    << "is branching from branch" << branchFrom
                    << "but the latter doesn't exist. Can't continue.";
        return EXIT_FAILURE;
    }

    QByteArray branchFromRef = ":" + QByteArray::number(mark);
    if (!mark) {
        qWarning() << "WARN:" << branch << "in repository" << name << "is branching but no exported commits exist in repository"
                << "creating an empty branch.";
        branchFromRef = branchFrom.toUtf8();
        if (!branchFromRef.startsWith("refs/"))
            branchFromRef.prepend("refs/heads/");
        branchFromDesc += ", deleted/unknown";
    }

    qDebug() << "Creating branch:" << branch << "from" << branchFrom << "(" << branchRevNum << branchFromDesc << ")";

    // Preserve note
    branchNotes[branch] = branchNotes.value(branchFrom);

    return resetBranch(branch, revnum, mark, branchFromRef, branchFromDesc);
}

int FastImportRepository::createBranch(const QString &branch, int revnum,
                                       const QString &tree_hash, Repository::Transaction* txn)
{
    qDebug() << "Creating branch:" << branch << "without parent (from tree" << tree_hash << ")";

    // Preserve note
    //branchNotes[branch] = branchNotes.value(tree_hash);

    QByteArray branchRef = branch.toUtf8();
    if (!branchRef.startsWith("refs/"))
        branchRef.prepend("refs/heads/");

    Branch &br = branches[branch];
    br.created = revnum;
    br.commits.append(revnum);
    br.marks.append(0);

    QByteArray cmd = "reset " + branchRef + /*"\nfrom " + resetTo + */"\n\n"
                     "progress SVN r" + QByteArray::number(revnum)
                     + " branch " + branch.toUtf8() + " = " + tree_hash.toUtf8()
                     + "\n\n";
    resetBranches.append(cmd);
    resetBranchNames.insert(branchRef);
    ((FastImportRepository::Transaction *)txn)->resetFromTree.append("M 040000 " + tree_hash.toUtf8() + " \n");

    return EXIT_SUCCESS;
}

int FastImportRepository::createBranch(const QString &branch, int revnum,
                                       const QString &branchFrom, int branchRevNum,
                                       const QString &tree_hash, Repository::Transaction* txn)
{
    QByteArray branchFromDesc = "from branch " + branchFrom.toUtf8();
    long long mark = markFrom(branchFrom, branchRevNum, branchFromDesc);

    if (mark == -1) {
        qCritical() << branch << "in repository" << name
                    << "is branching from branch" << branchFrom
                    << "but the latter doesn't exist. Can't continue.";
        return EXIT_FAILURE;
    }

    QByteArray branchFromRef = ":" + QByteArray::number(mark);
    if (!mark) {
        qWarning() << "WARN:" << branch << "in repository" << name << "is branching but no exported commits exist in repository"
                << "creating an empty branch.";
        branchFromRef = branchFrom.toUtf8();
        if (!branchFromRef.startsWith("refs/"))
            branchFromRef.prepend("refs/heads/");
        branchFromDesc += ", deleted/unknown";
    }

    qDebug() << "Creating branch:" << branch << "from" << branchFrom << "(" << branchRevNum << branchFromDesc << ")" << " (from tree" << tree_hash << ")";

    // Preserve note
    branchNotes[branch] = branchNotes.value(branchFrom);

    // This is a copy of resetBranch()
    QByteArray branchRef = branch.toUtf8();
    if (!branchRef.startsWith("refs/"))
        branchRef.prepend("refs/heads/");

    Branch &br = branches[branch];
    br.created = revnum;
    br.commits.append(revnum);
    br.marks.append(mark);

    QByteArray cmd = "reset " + branchRef + "\nfrom " + branchFromRef + "\n\n"
                     "progress SVN r" + QByteArray::number(revnum)
                     + " branch " + branch.toUtf8() + " = " + tree_hash.toUtf8()
                     + "\n\n";
    resetBranches.append(cmd);
    resetBranchNames.insert(branchRef);
    ((FastImportRepository::Transaction *)txn)->resetFromTree.append("M 040000 " + tree_hash.toUtf8() + " \n");

    return EXIT_SUCCESS;
}

int FastImportRepository::deleteBranch(const QString &branch, int revnum)
{
    static QByteArray null_sha(40, '0');
    return resetBranch(branch, revnum, 0, null_sha, "delete");
}

int FastImportRepository::resetBranch(const QString &branch, int revnum, mark_t mark, const QByteArray &resetTo, const QByteArray &comment)
{
    QByteArray branchRef = branch.toUtf8();
    if (!branchRef.startsWith("refs/"))
        branchRef.prepend("refs/heads/");

    Branch &br = branches[branch];
    QByteArray backupCmd;
    if (br.created && br.created != revnum && !br.marks.isEmpty() && br.marks.last()) {
        QByteArray backupBranch;
        if ((comment == "delete") && branchRef.startsWith("refs/heads/"))
            backupBranch = "refs/tags/backups/" + branchRef.mid(11) + "@" + QByteArray::number(revnum);
        else
            backupBranch = "refs/backups/r" + QByteArray::number(revnum) + branchRef.mid(4);
        qWarning() << "WARN: backing up branch" << branch << "to" << backupBranch;

        backupCmd = "reset " + backupBranch + "\nfrom " + branchRef + "\n\n";
    }

    br.created = revnum;
    br.commits.append(revnum);
    br.marks.append(mark);

    QByteArray cmd = "reset " + branchRef + "\nfrom " + resetTo + "\n\n"
                     "progress SVN r" + QByteArray::number(revnum)
                     + " branch " + branch.toUtf8() + " = :" + QByteArray::number(mark)
                     + " # " + comment + "\n\n";
    if(comment == "delete") {
        deletedBranches.append(backupCmd).append(cmd);
        deletedBranchNames.insert(branchRef);
    } else {
        resetBranches.append(backupCmd).append(cmd);
        resetBranchNames.insert(branchRef);
    }

    return EXIT_SUCCESS;
}

void FastImportRepository::commit()
{
    if (deletedBranches.isEmpty() && resetBranches.isEmpty()) {
        return;
    }
    startFastImport();
    fastImport.write(deletedBranches);
    fastImport.write(resetBranches);
    deletedBranches.clear();
    resetBranches.clear();
    QSet<QString>::ConstIterator it = deletedBranchNames.constBegin();
    for ( ; it != deletedBranchNames.constEnd(); ++it) {
        QString tagName = *it;
        if (resetBranchNames.contains(tagName))
            continue;
        if (tagName.startsWith("refs/tags/"))
            tagName.remove(0, 10);
        if (annotatedTags.remove(tagName) > 0) {
            qDebug() << "Removing annotated tag" << tagName << "for" << name;
        }
    }
    deletedBranchNames.clear();
    resetBranchNames.clear();
}

Repository::Transaction *FastImportRepository::newTransaction(const QString &branch, const QString &svnprefix,
                                                              int revnum)
{
    if (!branches.contains(branch)) {
        qWarning() << "WARN: Transaction:" << branch << "is not a known branch in repository" << name << endl
                   << "Going to create it automatically";
    }

    Transaction *txn = new Transaction;
    txn->repository = this;
    txn->branch = branch.toUtf8();
    txn->svnprefix = svnprefix.toUtf8();
    txn->datetime = 0;
    txn->revnum = revnum;

    static auto n = CommandLineParser::instance()->optionArgument(QLatin1String("commit-interval"), QLatin1String("25000")).toInt();
    if (++commitCount % n == 0) {
        startFastImport();
        // write everything to disk every 10000 commits
        fastImport.write("checkpoint\n");
        qDebug() << "checkpoint!, marks file truncated";
    }
    outstandingTransactions++;
    return txn;
}

void FastImportRepository::forgetTransaction(Transaction * /*unused*/)
{
    if (!--outstandingTransactions)
        next_file_mark = maxMark - 1;
}

void FastImportRepository::createAnnotatedTag(const QString &ref, const QString &svnprefix,
                                              int revnum,
                                              const QByteArray &author, uint dt,
                                              const QByteArray &log)
{
    QString tagName = ref;
    if (tagName.startsWith("refs/tags/"))
        tagName.remove(0, 10);

    if (!annotatedTags.contains(tagName)) {
        printf("\nCreating annotated tag %s (%s) for %s\n", qPrintable(tagName), qPrintable(ref), qPrintable(name));
    } else {
        // Log this warning only once per tagname
        static QSet<QString> logged_already_;
        if (!logged_already_.contains(tagName)) {
            logged_already_.insert(tagName);
            printf("\nRe-creating annotated tag %s for %s\n", qPrintable(tagName), qPrintable(name));
        }
    }

    AnnotatedTag &tag = annotatedTags[tagName];
    tag.supportingRef = ref;
    tag.svnprefix = svnprefix.toUtf8();
    tag.revnum = revnum;
    tag.author = author;
    tag.log = log;
    tag.dt = dt;
}

void FastImportRepository::finalizeTags()
{
    if (annotatedTags.isEmpty())
        return;

    if (!CommandLineParser::instance()->contains("dry-run") && !CommandLineParser::instance()->contains("create-dump")) {
        QFile annotatedTagsFile(name + "/" + annotatedTagsFileName(name));
        annotatedTagsFile.open(QIODevice::WriteOnly);
        QDataStream annotatedTagsStream(&annotatedTagsFile);
        annotatedTagsStream << annotatedTags;
        annotatedTagsFile.close();
    }

    printf("Finalising annotated tags for %s...", qPrintable(name));
    startFastImport();

    // Plain sort of course puts release/4.10 before release/4.9, we rewrite
    // them later anyway, so this should be fine, except when there's a merge
    // conflict.
    auto sorted_tags = annotatedTags.keys();
    std::sort(sorted_tags.begin(), sorted_tags.end());
    for (const auto &tagName : sorted_tags) {
        const AnnotatedTag &tag = annotatedTags[tagName];

        QByteArray message = tag.log;
        if (!message.endsWith('\n'))
            message += '\n';
        if (CommandLineParser::instance()->contains("add-metadata"))
            message += "\n" + formatMetadataMessage(tag.svnprefix, tag.revnum, tagName.toUtf8());

        {
            QByteArray branchRef = tag.supportingRef.toUtf8();
            if (!branchRef.startsWith("refs/"))
                branchRef.prepend("refs/heads/");

            QByteArray s = "progress Creating annotated tag " + tagName.toUtf8() + " from ref " + branchRef + "\n"
              + "tag " + tagName.toUtf8() + "\n"
              + "from " + branchRef + "\n"
              + "tagger " + tag.author + ' ' + QByteArray::number(tag.dt) + " +0000" + "\n"
              + "data " + QByteArray::number( message.length() ) + "\n";
            fastImport.write(s);
        }

        fastImport.write(message);
        fastImport.putChar('\n');
        if (!fastImport.waitForBytesWritten(-1))
            qFatal("Failed to write to process 1: %s", qPrintable(fastImport.errorString()));

        // Append note to the tip commit of the supporting ref. There is no
        // easy way to attach a note to the tag itself with fast-import.
        if (CommandLineParser::instance()->contains("add-metadata-notes")) {
            Repository::Transaction *txn = newTransaction(tag.supportingRef, tag.svnprefix, tag.revnum);
            txn->setAuthor(tag.author);
            txn->setDateTime(tag.dt);
            bool written = txn->commitNote(formatMetadataMessage(tag.svnprefix, tag.revnum, tagName.toUtf8()), true);
            delete txn;

            if (written && !fastImport.waitForBytesWritten(-1))
                qFatal("Failed to write to process 2: %s", qPrintable(fastImport.errorString()));
        }

        printf(" %s", qPrintable(tagName));
        fflush(stdout);
    }
    // commitNote didn't actually commit anything, fool! But now we have all
    // the potential refs/notes/commits gathered, can sort them and dump them
    // out.
    std::stable_sort(
        delayed_notes.begin(), delayed_notes.end(),
        [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &n : delayed_notes) {
        fastImport.write(*n.second);
    }

    while (fastImport.bytesToWrite())
        if (!fastImport.waitForBytesWritten(-1))
            qFatal("Failed to write to process 3: %s", qPrintable(fastImport.errorString()));
    printf("\n");
}

void FastImportRepository::saveBranchNotes()
{
    if (branchNotes.isEmpty())
        return;

    if (!CommandLineParser::instance()->contains("dry-run") && !CommandLineParser::instance()->contains("create-dump")) {
        QFile branchNotesFile(name + "/" + branchNotesFileName(name));
        branchNotesFile.open(QIODevice::WriteOnly);
        QDataStream branchNotesStream(&branchNotesFile);
        branchNotesStream << branchNotes;
        branchNotesFile.close();
    }
}

QByteArray
FastImportRepository::msgFilter(const QByteArray& msg)
{
    // Instead of forking for every revision, we apply our msg filter directly here.
    QByteArray output;
    output.reserve(msg.size());
    QList<QByteArray> lines = msg.split('\n');
    while (lines.last().isEmpty() && lines.length() > 1) {
        lines.removeLast();
    }
    for (const auto& line : lines) {
        if (line.endsWith("those below, will be ignored--") ||
                line.startsWith("> Description of fields to fill in above") ||
                line.startsWith("> PR:            If a GNATS PR is affected by the change") ||
                line.startsWith("> Submitted by:  If someone else sent in the change") ||
                line.startsWith("_M   ")) {
            return output;
        } else {
            output.append(line);
            output.append('\n');
        }
    }
    return output;
}

void FastImportRepository::startFastImport()
{
    processCache.touch(this);

    if (fastImport.state() == QProcess::NotRunning) {
        if (processHasStarted)
            qFatal("git-fast-import has been started once and crashed?");
        processHasStarted = true;

        // start the process
        QString marksFile = marksFileName(name);
        QStringList marksOptions;
        marksOptions << "--import-marks=" + marksFile;
        marksOptions << "--export-marks=" + marksFile;
        marksOptions << "--force";

        fastImport.setStandardOutputFile(logFileName(name), QIODevice::Append);
        fastImport.setProcessChannelMode(QProcess::MergedChannels);

        if (!CommandLineParser::instance()->contains("dry-run") && !CommandLineParser::instance()->contains("create-dump")) {
            fastImport.start("git", QStringList() << "fast-import" << marksOptions);
        } else {
            fastImport.start("cat", QStringList());
        }
        fastImport.waitForStarted(-1);

        reloadBranches();
    }
}

QByteArray Repository::formatMetadataMessage(const QByteArray &svnprefix, int revnum, const QByteArray &tag)
{
    QByteArray msg = "svn path=" + svnprefix + "; revision=" + QByteArray::number(revnum);
    if (!tag.isEmpty())
        msg += "; tag=" + tag;
    msg += "\n";
    return msg;
}

bool FastImportRepository::branchExists(const QString& branch) const
{
    return branches.contains(branch);
}

const QByteArray FastImportRepository::branchNote(const QString& branch) const
{
    return branchNotes.value(branch);
}

void FastImportRepository::setBranchNote(const QString& branch, const QByteArray& noteText)
{
    if (branches.contains(branch)) {
        branchNotes[branch] = noteText;
    }
}

bool FastImportRepository::hasPrefix() const
{
    return !prefix.isEmpty();
}

QString FastImportRepository::getName() const
{
    return name;
}

Repository *FastImportRepository::getEffectiveRepository()
{
    return this;
}

FastImportRepository::Transaction::~Transaction()
{
    repository->forgetTransaction(this);
}

void FastImportRepository::Transaction::setAuthor(const QByteArray &a)
{
    author = a;
}

void FastImportRepository::Transaction::setDateTime(uint dt)
{
    datetime = dt;
}

void FastImportRepository::Transaction::setLog(const QByteArray &l)
{
    log = l;
}

void FastImportRepository::Transaction::noteCopyFromBranch(const QString &branchFrom, int branchRevNum, bool allow_heuristic)
{
    // We are resetting the branch from a nameless tree, don't spew out warnings.
    if (!resetFromTree.isEmpty()) {
        return;
    }
    static QByteArray dummy;
    long long mark = repository->markFrom(branchFrom, branchRevNum, dummy);
    Q_ASSERT(dummy.isEmpty());

    if (mark == -1) {
        // This hack is now needed, as we for example turn the SVN branch
        // /vendor/sendmail/dist into a git branch vendor/sendmail (sans dist).
        // This is nice, but needs an ugly and brittle hack. An alternative
        // would be to keep it as vendor/sendmail/dist during the conversion,
        // but do a last-pass fixup, renaming all refs called vendor/foo/dist
        // to vendor/foo (unless they still have tags in the same namespace).
        // ... or something.
        if (branchFrom.endsWith("/dist")) {
            QString non_dist = branchFrom;
            non_dist.truncate(non_dist.lastIndexOf("/dist"));
            qWarning() << "WARN:" << branch << "is copying from branch" << branchFrom
                << "but the latter doesn't exist.  Trying with" << non_dist << "instead.";
            mark = repository->markFrom(non_dist, branchRevNum, dummy);
        }
        if (mark == -1) {
            qWarning() << "WARN:" << branch << "is copying from branch" << branchFrom
                << "but the latter doesn't exist.  Continuing, assuming the files exist.";
        }
    } else if (mark == 0) {
        qWarning() << "WARN: Unknown revision r" << QByteArray::number(branchRevNum)
            << ".  Continuing, assuming the files exist.";
    } else {
        // Log this warning only once per revnum
        static QSet<int> logged_already_;
        QStringList log = QStringList()
                          << "WARN: repository " + repository->name +
                                 " branch " + branch +
                                 " has some files copied from " + branchFrom +
                                 "@" + QByteArray::number(branchRevNum);
        if (!logged_already_.contains(qHash(log))) {
            logged_already_.insert(qHash(log));
            qWarning() << "WARN: repository " + repository->name + " branch " + branch + " has some files copied from " + branchFrom + "@" + QByteArray::number(branchRevNum);
        }
    }

    // We might have found a better mark sans dist suffix.
    if (mark > 0) {
        QByteArray branchRef = branch;
        if (!branchRef.startsWith("refs/"))
            branchRef.prepend("refs/heads/");

        // Might not be a merge, we might create the branch from a too old
        // revision, SVN does this all the time when creating a tag by copying
        // stuff over, often the top-level dir of the copy will be from an
        // older revision and the files then come for a newer one (but often
        // from different ones as well).
        // This hack is is based on the assumption that resetBranch is only
        // ever called once per branch creation, i.e. no branch is created
        // having 2 parents (I think, tbh, I'm not sure what the implications
        // are.) Anyway, we blast away the branch reset whenever we find a
        // higher numbered mark (on the same branch).
        // This revision creates the branch, make sure the mark is the highest possible.
        if (repository->branches.contains(branch)
                && revnum == repository->branches[branch].created
                && repository->resetBranchNames.contains(branchRef)) {
            const QString rb = repository->resetBranches;
            const Branch &br = repository->branches[branch];
            if (br.marks.last() < mark && rb.contains("from branch "+branchFrom)) {
                if (!allow_heuristic) {
                    qDebug() << "WARN: found branchpoint from lower mark, ignoring due to manual rule override";
                    return;
                }
                qDebug() << "WARN: found branchpoint from lower mark, about to recreate branch from different revision";
                //qDebug() << "\n" << repository->resetBranches << "\n";
                repository->resetBranches.clear();
                repository->createBranch(branch, revnum, branchFrom, branchRevNum);
                return;
            }
        } else if (merge_map.contains(branchFrom)) {
            const long long old_mark = merge_map[branchFrom];
            if (old_mark < mark) {
                qDebug() << "bumping to"
                    << branchFrom + "@" + QByteArray::number(branchRevNum) << ":" << mark
                    << "from" << old_mark << "as a merge point";
                merges.removeOne(old_mark);
                merges.push_back(mark);
                merge_map[branchFrom] = mark;
            }
        } else {
            merges.push_back(mark);
            merge_map[branchFrom] = mark;
            qDebug() << "adding" << branchFrom + "@" + QByteArray::number(branchRevNum) << ":" << mark << "as a merge point";
        }
    }
}

void FastImportRepository::Transaction::deleteFile(const QString &path)
{
    QString pathNoSlash = repository->prefix + path;
    if(pathNoSlash.endsWith('/'))
        pathNoSlash.chop(1);
    deletedFiles.append(pathNoSlash);
}

void FastImportRepository::Transaction::renameFile(const QString &from, const QString &to)
{
    // Due to rule order and export ordering, SVN might have caused a file
    // deletion, but we later want to rename it to something else to patch up
    // CVS repo copies and the like. So undelete the files before renaming
    // them.
    QString fromNoSlash = repository->prefix + from;
    QString toNoSlash = repository->prefix + to;
    if (fromNoSlash.endsWith('/'))
      fromNoSlash.chop(1);
    if (toNoSlash.endsWith('/'))
      toNoSlash.chop(1);
    deletedFiles.removeOne(fromNoSlash);
    renamedFiles.append(QPair<QString, QString>(fromNoSlash, toNoSlash));
}

QIODevice *FastImportRepository::Transaction::addFile(const QString &path, int mode, qint64 length)
{
    mark_t mark = repository->next_file_mark--;

    // in case the two mark allocations meet, we might as well just abort
    Q_ASSERT(mark > repository->last_commit_mark + 1);

    if (modifiedFiles.capacity() == 0)
        modifiedFiles.reserve(2048);
    modifiedFiles.append("M ");
    modifiedFiles.append(QByteArray::number(mode, 8));
    modifiedFiles.append(" :");
    modifiedFiles.append(QByteArray::number(mark));
    modifiedFiles.append(' ');
    modifiedFiles.append(repository->prefix + path.toUtf8());
    modifiedFiles.append("\n");

    // it is returned for being written to, so start the process in any case
    repository->startFastImport();
    if (!CommandLineParser::instance()->contains("dry-run")) {
        repository->fastImport.writeNoLog("blob\nmark :");
        repository->fastImport.writeNoLog(QByteArray::number(mark));
        repository->fastImport.writeNoLog("\ndata ");
        repository->fastImport.writeNoLog(QByteArray::number(length));
        repository->fastImport.writeNoLog("\n", 1);
    }

    return &repository->fastImport;
}

bool FastImportRepository::Transaction::commitNote(const QByteArray &noteText, bool append, const QByteArray &commit)
{
    QByteArray branchRef = branch;
    if (!branchRef.startsWith("refs/"))
    {
        branchRef.prepend("refs/heads/");
    }
    const QByteArray &commitRef = commit.isNull() ? branchRef : commit;
    QByteArray message = "Adding Git note for current " + branchRef + "\n";
    QByteArray text = noteText;
    if (noteText[noteText.size() - 1] != '\n')
    {
        text += '\n';
    }

    QByteArray branchNote = repository->branchNote(branch);
    if (!branchNote.isEmpty() && (branchNote[branchNote.size() - 1] != '\n'))
    {
        branchNote += '\n';
    }
    if (append && commit.isNull() &&
        repository->branchExists(branch) &&
        !branchNote.isEmpty())
    {
        //qDebug() << "\nbranchNote for" << branch << "is" << branchNote << "and text is" << text;
        if (text.startsWith(branchNote.chopped(1))) {
            // text stays unaltered
            //message = "Replacing Git note for current " + branchRef + "\n";
        } else {
            text = branchNote + text;
            message = "Appending Git note for current " + branchRef + "\n";
        }
    }

    repository->setBranchNote(QString::fromUtf8(branch), text);

    auto s = std::make_unique<QByteArray>("");
    s->append("commit refs/notes/commits\n");
    s->append("mark :" + QByteArray::number(maxMark) + "\n");
    s->append("committer svn2git <svn2git@FreeBSD.org> " + QString::number(datetime) + " +0000" + "\n");
    s->append("data " + QString::number(message.length()) + "\n");
    s->append(message + "\n");
    s->append("N inline " + commitRef + "\n");
    s->append("data " + QString::number(text.length()) + "\n");
    s->append(text + "\n");
    repository->delayed_notes.emplace_back(datetime, std::move(s));

    // We delay this till the end so we can sort the notes into the regular
    // refs/notes/commits stream.
    return false;
}

int FastImportRepository::Transaction::commit()
{
    foreach (QString branchName, repository->branches.keys())
    {
        if (branchName.toUtf8().startsWith(branch + "/") || branch.startsWith((branchName + "/").toUtf8()))
        {
            qCritical() << "Branch" << branch << "conflicts with already existing branch" << branchName;
            return EXIT_FAILURE;
        }
    }

    repository->startFastImport();

    // We might be tempted to use the SVN revision number as the fast-import commit mark.
    // However, a single SVN revision can modify multiple branches, and thus lead to multiple
    // commits in the same repo.  So, we need to maintain a separate commit mark counter.
    mark_t  mark = ++repository->last_commit_mark;

    // in case the two mark allocations meet, we might as well just abort
    Q_ASSERT(mark < repository->next_file_mark - 1);

    // create the commit message
    QByteArray message = log;
    if (!message.endsWith('\n'))
        message += '\n';
    if (CommandLineParser::instance()->contains("add-metadata"))
        message += "\n" + Repository::formatMetadataMessage(svnprefix, revnum);

    // Call external message filter if provided
    message = repository->msgFilter(message);

    mark_t parentmark = 0;
    Branch &br = repository->branches[branch];
    if (br.created && !br.marks.isEmpty() && br.marks.last()) {
        parentmark = br.marks.last();
    } else {
        if (revnum > 1) {
            // Any branch at revision 1 isn't going to exist, so lets not alarm the user.
            qWarning() << "WARN: Branch" << branch << "in repository" << repository->name << "doesn't exist at revision"
                       << revnum << "-- did you resume from the wrong revision?";
        }
        br.created = revnum;
    }
    br.commits.append(revnum);
    br.marks.append(mark);

    QByteArray branchRef = branch;
    if (!branchRef.startsWith("refs/"))
        branchRef.prepend("refs/heads/");

    QByteArray s("");
    s.append("commit " + branchRef + "\n");
    s.append("mark :" + QByteArray::number(mark) + "\n");
    s.append("committer " + author + " " + QString::number(datetime).toUtf8() + " +0000" + "\n");
    s.append("data " + QString::number(message.length()) + "\n");
    s.append(message + "\n");
    repository->fastImport.write(s);

    // note some of the inferred merges
    QByteArray desc = "";
    mark_t i = !!parentmark;        // if parentmark != 0, there's at least one parent

    foreach (const mark_t merge, merges) {
        if (merge == parentmark) {
            qDebug() << "Skipping marking" << merge << "as a merge point as it matches the parent";
            continue;
        }
        ++i;

        QByteArray m = " :" + QByteArray::number(merge);
        desc += m;
        repository->fastImport.write("merge" + m + "\n");
    }
    // If we suppress the branchpoint, we still need to start out with the
    // previous tree, all we want is to suppress the creation of a parent.
    // Basically we want what `merge` does, except in reverse, an `unmerge` so
    // to speak. We can do this by providing the data from the previous tree
    // via mark first. Sadly, again, that mark is a commit mark and fast-import
    // isn't clever enough to treat a commit mark as just taking the tree of
    // that commit. We use the tree-hash instead, which should be fairly
    // stable.
    if (resetFromTree.size() > 0) {
        repository->fastImport.write(resetFromTree);
    }

    // write the file deletions
    if (deletedFiles.contains(""))
        repository->fastImport.write("deleteall\n");
    else
        foreach (QString df, deletedFiles)
            repository->fastImport.write("D " + df.toUtf8() + "\n");

    // write the file modifications
    repository->fastImport.write(modifiedFiles);

    // run through the rename pairs, potentially deleting paths
    QPair<QString, QString> pair;
    foreach (pair, renamedFiles) {
        const QString& from = pair.first;
        const QString& to = pair.second;
        // We want our delete fixups to happen *after* the modifications were
        // written, so that we can undo CVS repo copies. We cannot abuse the
        // regular delete mechanics above though, as that would interfere
        // horribly with the regular SVN export. So basically handle renames to
        // "/dev/null" as such post-export deletes.
        if (to == "" || to == "/dev/null") {
            repository->fastImport.write("D " + from.toUtf8() + "\n");
        } else {
            repository->fastImport.write("R " + from.toUtf8() + " " + to.toUtf8() + "\n");
        }
    }

    repository->fastImport.write("\nprogress SVN r" + QByteArray::number(revnum)
                                 + " branch " + branch + " = :" + QByteArray::number(mark)
                                 + (desc.isEmpty() ? "" : " # merge from") + desc
                                 + "\n\n");
    printf(" %d modifications from SVN %s to %s/%s",
           deletedFiles.count() + modifiedFiles.count('\n'), svnprefix.data(),
           qPrintable(repository->name), branch.data());

    // Commit metadata note if requested
    // All our refs/tags are annotated and will be exported last. This is to
    // avoid duplicate notes commits that later cannot be readily sorted into
    // chronological order.
    if (CommandLineParser::instance()->contains("add-metadata-notes") && !branch.startsWith("refs/tags/")) {
        commitNote(Repository::formatMetadataMessage(svnprefix, revnum), false, ":"+QByteArray::number(mark));
    }

    while (repository->fastImport.bytesToWrite())
        if (!repository->fastImport.waitForBytesWritten(-1))
            qFatal("Failed to write to process: %s for repository %s", qPrintable(repository->fastImport.errorString()), qPrintable(repository->name));

    return EXIT_SUCCESS;
}
