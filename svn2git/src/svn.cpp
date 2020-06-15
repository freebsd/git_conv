/*
 *  Copyright (C) 2007  Thiago Macieira <thiago@kde.org>
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

/*
 * Based on svn-fast-export by Chris Lee <clee@kde.org>
 * License: MIT <http://www.opensource.org/licenses/mit-license.php>
 * URL: git://repo.or.cz/fast-import.git http://repo.or.cz/w/fast-export.git
 */

#define _XOPEN_SOURCE
#define _LARGEFILE_SUPPORT
#define _LARGEFILE64_SUPPORT

#include "svn.h"
#include "CommandLineParser.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <apr_lib.h>
#include <apr_getopt.h>
#include <apr_general.h>

#include <svn_fs.h>
#include <svn_pools.h>
#include <svn_repos.h>
#include <svn_types.h>
#include <svn_version.h>

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QRegularExpression>

#include "repository.h"

#undef SVN_ERR
#define SVN_ERR(expr) SVN_INT_ERR(expr)

#if SVN_VER_MAJOR == 1 && SVN_VER_MINOR < 9
#define svn_stream_read_full svn_stream_read
#endif

typedef QList<Rules::Match> MatchRuleList;
typedef QHash<QString, Repository *> RepositoryHash;
typedef QHash<QByteArray, QByteArray> IdentityHash;

class AprAutoPool
{
    apr_pool_t *pool;
    AprAutoPool(const AprAutoPool &);
    AprAutoPool &operator=(const AprAutoPool &);
public:
    inline AprAutoPool(apr_pool_t *parent = NULL)
        {
            pool = svn_pool_create(parent);
        }
        inline ~AprAutoPool()
        {
            svn_pool_destroy(pool);
        }

    inline void clear() { svn_pool_clear(pool); }
    inline apr_pool_t *data() const { return pool; }
    inline operator apr_pool_t *() const { return pool; }
};

class SvnPrivate
{
public:
    QList<MatchRuleList> allMatchRules;
    RepositoryHash repositories;
    IdentityHash identities;
    QString userdomain;

    SvnPrivate(const QString &pathToRepository);
    ~SvnPrivate();
    int youngestRevision();
    int exportRevision(int revnum);

    int openRepository(const QString &pathToRepository);

private:
    AprAutoPool global_pool;
    AprAutoPool scratch_pool;
    svn_fs_t *fs;
    svn_revnum_t youngest_rev;
    QString svn_repo_path;
};

void Svn::initialize()
{
    // initialize APR or exit
    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "You lose at apr_initialize().\n");
        exit(1);
    }

    // static destructor
    static struct Destructor { ~Destructor() { apr_terminate(); } } destructor;
}

Svn::Svn(const QString &pathToRepository)
    : d(new SvnPrivate(pathToRepository))
{
}

Svn::~Svn()
{
    delete d;
}

void Svn::setMatchRules(const QList<MatchRuleList> &allMatchRules)
{
    d->allMatchRules = allMatchRules;
}

void Svn::setRepositories(const RepositoryHash &repositories)
{
    d->repositories = repositories;
}

void Svn::setIdentityMap(const IdentityHash &identityMap)
{
    d->identities = identityMap;
}

void Svn::setIdentityDomain(const QString &identityDomain)
{
    d->userdomain = identityDomain;
}

int Svn::youngestRevision()
{
    return d->youngestRevision();
}

bool Svn::exportRevision(int revnum)
{
    return d->exportRevision(revnum) == EXIT_SUCCESS;
}

SvnPrivate::SvnPrivate(const QString &pathToRepository)
    : global_pool(NULL) , scratch_pool(NULL), svn_repo_path(pathToRepository)
{
    if( openRepository(pathToRepository) != EXIT_SUCCESS) {
        qCritical() << "Failed to open repository";
        exit(1);
    }
    svn_repo_path.prepend("file:///");

    // get the youngest revision
    svn_fs_youngest_rev(&youngest_rev, fs, global_pool);
}

SvnPrivate::~SvnPrivate() {}

int SvnPrivate::youngestRevision()
{
    return youngest_rev;
}

int SvnPrivate::openRepository(const QString &pathToRepository)
{
    svn_repos_t *repos;
    QString path = pathToRepository;
    while (path.endsWith('/')) // no trailing slash allowed
        path = path.mid(0, path.length()-1);
#if SVN_VER_MAJOR == 1 && SVN_VER_MINOR < 9
    SVN_ERR(svn_repos_open2(&repos, QFile::encodeName(path), NULL, global_pool));
#else
    SVN_ERR(svn_repos_open3(&repos, QFile::encodeName(path), NULL, global_pool, scratch_pool));
#endif
    fs = svn_repos_fs(repos);

    return EXIT_SUCCESS;
}

enum RuleType { AnyRule = 0, NoIgnoreRule = 0x01, NoRecurseRule = 0x02 };

static MatchRuleList::ConstIterator
findMatchRule(const MatchRuleList &matchRules, int revnum, const QString &current,
              int ruleMask = AnyRule)
{
    MatchRuleList::ConstIterator it = matchRules.constBegin(),
                                end = matchRules.constEnd();
    for ( ; it != end; ++it) {
        if (it->minRevision > revnum)
            continue;
        if (it->maxRevision != -1 && it->maxRevision < revnum)
            continue;
        if (it->action == Rules::Match::Ignore && ruleMask & NoIgnoreRule)
            continue;
        if (it->action == Rules::Match::Recurse && ruleMask & NoRecurseRule)
            continue;
        if (it->rx.indexIn(current) == 0) {
            Stats::instance()->ruleMatched(*it, revnum);
            return it;
        }
    }

    // no match
    return end;
}

static int pathMode(svn_fs_root_t *fs_root, const char *pathname, apr_pool_t *pool)
{
    svn_string_t *propvalue;
    SVN_ERR(svn_fs_node_prop(&propvalue, fs_root, pathname, "svn:executable", pool));
    int mode = 0100644;
    if (propvalue)
        mode = 0100755;

    return mode;
}

svn_error_t *QIODevice_write(void *baton, const char *data, apr_size_t *len)
{
    QIODevice *device = reinterpret_cast<QIODevice *>(baton);
    device->write(data, *len);

    while (device->bytesToWrite() > 32*1024) {
        if (!device->waitForBytesWritten(-1)) {
            qFatal("Failed to write to process: %s", qPrintable(device->errorString()));
            return svn_error_createf(APR_EOF, SVN_NO_ERROR, "Failed to write to process: %s",
                                     qPrintable(device->errorString()));
        }
    }
    return SVN_NO_ERROR;
}

static svn_stream_t *streamForDevice(QIODevice *device, apr_pool_t *pool)
{
    svn_stream_t *stream = svn_stream_create(device, pool);
    svn_stream_set_write(stream, QIODevice_write);

    return stream;
}

static int dumpBlob(Repository::Transaction *txn, svn_fs_root_t *fs_root,
                    const char *pathname, const QString &finalPathName, apr_pool_t *pool)
{
    AprAutoPool dumppool(pool);
    // what type is it?
    int mode = pathMode(fs_root, pathname, dumppool);

    svn_filesize_t stream_length;

    SVN_ERR(svn_fs_file_length(&stream_length, fs_root, pathname, dumppool));

    svn_stream_t *in_stream, *out_stream;
    if (!CommandLineParser::instance()->contains("dry-run")) {
        // open the file
        SVN_ERR(svn_fs_file_contents(&in_stream, fs_root, pathname, dumppool));
    }

    // maybe it's a symlink?
    svn_string_t *propvalue;
    SVN_ERR(svn_fs_node_prop(&propvalue, fs_root, pathname, "svn:special", dumppool));
    if (propvalue) {
        apr_size_t len = strlen("link ");
        if (!CommandLineParser::instance()->contains("dry-run")) {
            QByteArray buf;
            buf.reserve(len);
            SVN_ERR(svn_stream_read_full(in_stream, buf.data(), &len));
            if (len == strlen("link ") && strncmp(buf, "link ", len) == 0) {
                mode = 0120000;
                stream_length -= len;
            } else {
                //this can happen if a link changed into a file in one commit
                qWarning("file %s is svn:special but not a symlink", pathname);
                // re-open the file as we tried to read "link "
                svn_stream_close(in_stream);
                SVN_ERR(svn_fs_file_contents(&in_stream, fs_root, pathname, dumppool));
            }
        }
    }

    QIODevice *io = txn->addFile(finalPathName, mode, stream_length);

    if (!CommandLineParser::instance()->contains("dry-run")) {
        // open a generic svn_stream_t for the QIODevice
        out_stream = streamForDevice(io, dumppool);
        SVN_ERR(svn_stream_copy3(in_stream, out_stream, NULL, NULL, dumppool));

        // print an ending newline
        io->putChar('\n');
    }

    return EXIT_SUCCESS;
}

static bool wasDir(svn_fs_t *fs, int revnum, const char *pathname, apr_pool_t *pool)
{
    AprAutoPool subpool(pool);
    svn_fs_root_t *fs_root;
    if (svn_fs_revision_root(&fs_root, fs, revnum, subpool) != SVN_NO_ERROR)
        return false;

    svn_boolean_t is_dir;
    if (svn_fs_is_dir(&is_dir, fs_root, pathname, subpool) != SVN_NO_ERROR)
        return false;

    return is_dir;
}

static int recursiveDumpDir(Repository::Transaction *txn, svn_fs_t *fs, svn_fs_root_t *fs_root,
                            const QByteArray &pathname, const QString &finalPathName,
                            apr_pool_t *pool, svn_revnum_t revnum,
                            const Rules::Match &rule, const MatchRuleList &matchRules,
                            bool ruledebug)
{
    if (!wasDir(fs, revnum, pathname.data(), pool)) {
        if (dumpBlob(txn, fs_root, pathname, finalPathName, pool) == EXIT_FAILURE)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }

    // get the dir listing
    apr_hash_t *entries;
    SVN_ERR(svn_fs_dir_entries(&entries, fs_root, pathname, pool));
    AprAutoPool dirpool(pool);

    // While we get a hash, put it in a map for sorted lookup, so we can
    // repeat the conversions and get the same git commit hashes.
    QMap<QByteArray, svn_node_kind_t> map;
    for (apr_hash_index_t *i = apr_hash_first(pool, entries); i; i = apr_hash_next(i)) {
        const void *vkey;
        void *value;
        apr_hash_this(i, &vkey, NULL, &value);
        svn_fs_dirent_t *dirent = reinterpret_cast<svn_fs_dirent_t *>(value);
        map.insertMulti(QByteArray(dirent->name), dirent->kind);
    }

    QMapIterator<QByteArray, svn_node_kind_t> i(map);
    while (i.hasNext()) {
        dirpool.clear();
        i.next();
        QByteArray entryName = pathname + '/' + i.key();
        QString entryFinalName = finalPathName + QString::fromUtf8(i.key());

        if (i.value() == svn_node_dir) {
            entryFinalName += '/';
            QString entryNameQString = entryName + '/';

            MatchRuleList::ConstIterator match = findMatchRule(matchRules, revnum, entryNameQString);
            if (match == matchRules.constEnd()) continue; // no match of parent repo? (should not happen)

            const Rules::Match &matchedRule = *match;
            if (matchedRule.action != Rules::Match::Export || matchedRule.repository != rule.repository) {
                if (ruledebug)
                    qDebug() << "recursiveDumpDir:" << entryNameQString << "skip entry for different/ignored repository";
                continue;
            }

            if (recursiveDumpDir(txn, fs, fs_root, entryName, entryFinalName, dirpool, revnum, rule, matchRules, ruledebug) == EXIT_FAILURE)
                return EXIT_FAILURE;
        } else if (i.value() == svn_node_file) {
            printf("+");
            fflush(stdout);
            if (dumpBlob(txn, fs_root, entryName, entryFinalName, dirpool) == EXIT_FAILURE)
                return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

time_t get_epoch(const char* svn_date)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    QByteArray date(svn_date, strlen(svn_date) - 8);
    strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
    return timegm(&tm);
}

class SvnRevision
{
public:
    AprAutoPool pool;
    QMap<QString, Repository::Transaction *> transactions;
    QList<MatchRuleList> allMatchRules;
    RepositoryHash repositories;
    IdentityHash identities;
    QString userdomain;

    svn_fs_t *fs;
    svn_fs_root_t *fs_root;
    int revnum;

    // must call fetchRevProps first:
    QByteArray authorident;
    QByteArray log;
    uint epoch;
    bool ruledebug;
    bool propsFetched;
    bool needCommit;

    QSet<int> logged_already_;

    QString& svn_repo_path;
    QString merge_from_branch_;
    QString merge_from_rev_;
    QSet<QString> to_branches_;

    QMap<QString, QSet<QString>> deletions_;
    QMap<QString, QMap<QString, QString>> renames_;

    // There are some handful of mergeinfo changes that are bogus and need to be skipped
    // r306199 - Revert svn:mergeinfo added inadvertantly in last commit r306197
    // r305318 - Handle missed mergeinfo by merging r305031 (the missing revision according to svn merge)
    // r299143
    // r288036 - Fixup mergeinfo on sys/contrib/ipfilter/netinet/ip_fil_freebsd.c.
    // r287679
    // r281784
    // r276402 - Remove "svn:mergeinfo" property that was dragged along when these files were svn copied in r273375.
    // r273097
    // r272415
    // r262613
    // r255958 - Add missing mergeinfo associated with r255852.
    // r208239 - Adjust svn:mergeinfo for revision 204546.  This commit moves mergeinfo to lib/ and removes mergeinfo on individual file.
    // r206971 - remove svn:mergeinfo properties committed during my MFCs.
    // r203808 - Migrate mergeinfo which was done on wrong target back to etc/ (203163).
    // r203516 - Fix mergeinfo from r197799
    // r202105
    // r198498 - Trim empty mergeinfo.
    // r197807 - Properly record merginfo for r197681 into lib/libc instead of lib/libc/gen.
    // r194300 
    // r190749 - Remove pointeless mergeinfo that crept in from r190633.
    // r190634 - Remove some pointless mergeinfo that is the result of doing a local 'svn cp'
    // r186082 - Bootstrapping merge history for resolver.
    // r185539 - Delete a bunch of empty mergeinfo records caused by local copies.
    // r182315 - Consolidate mergeinfo
    // r182274 - Move mergeinfo around.
    //
    // lots more, especially on stable/X branches

    SvnRevision(int revision, svn_fs_t *f, apr_pool_t *parent_pool, QString& svn_repo_path)
        : pool(parent_pool), fs(f), fs_root(0), revnum(revision), propsFetched(false), svn_repo_path(svn_repo_path)
    {
        ruledebug = CommandLineParser::instance()->contains( QLatin1String("debug-rules"));
    }

    int open()
    {
        SVN_ERR(svn_fs_revision_root(&fs_root, fs, revnum, pool));
        return EXIT_SUCCESS;
    }

    int prepareTransactions();
    int fetchRevProps();
    int commit();

    int exportEntry(const char *path, const svn_fs_path_change2_t *change, apr_hash_t *changes);
    int exportDispatch(const char *path, const svn_fs_path_change2_t *change,
                       const char *path_from, svn_revnum_t rev_from,
                       apr_hash_t *changes, const QString &current, const Rules::Match &rule,
                       const MatchRuleList &matchRules, apr_pool_t *pool);
    int exportInternal(const char *path, const svn_fs_path_change2_t *change,
                       const char *path_from, svn_revnum_t rev_from,
                       const QString &current, const Rules::Match &rule, const MatchRuleList &matchRules);
    int recurse(const char *path, const svn_fs_path_change2_t *change,
                const char *path_from, const MatchRuleList &matchRules, svn_revnum_t rev_from,
                apr_hash_t *changes, apr_pool_t *pool);
    int addGitIgnore(apr_pool_t *pool, const char *key, QString path,
                     svn_fs_root_t *fs_root, Repository::Transaction *txn, const char *content = NULL);
    int fetchIgnoreProps(QString *ignore, apr_pool_t *pool, const char *key, svn_fs_root_t *fs_root);
    int fetchUnknownProps(apr_pool_t *pool, const char *key, svn_fs_root_t *fs_root);
private:
    void splitPathName(const Rules::Match &rule, const QString &pathName, QString *svnprefix_p,
                       QString *repository_p, QString *effectiveRepository_p, QString *branch_p, QString *path_p);
    QString match_path_to_branch(const QString& path);
    bool maybeParseSimpleMergeinfo(const int revnum, QList<mergeinfo>* mi_list);
};

int SvnPrivate::exportRevision(int revnum)
{
    SvnRevision rev(revnum, fs, global_pool, svn_repo_path);
    rev.allMatchRules = allMatchRules;
    rev.repositories = repositories;
    rev.identities = identities;
    rev.userdomain = userdomain;

    // open this revision:
    printf("Exporting revision %d ", revnum);
    fflush(stdout);

    if (rev.open() == EXIT_FAILURE)
        return EXIT_FAILURE;

    if (rev.prepareTransactions() == EXIT_FAILURE)
        return EXIT_FAILURE;

    if (!rev.needCommit) {
        printf(" nothing to do\n");
        return EXIT_SUCCESS;    // no changes?
    }

    if (rev.commit() == EXIT_FAILURE)
        return EXIT_FAILURE;

    printf(" done\n");
    return EXIT_SUCCESS;
}

void SvnRevision::splitPathName(const Rules::Match &rule, const QString &pathName, QString *svnprefix_p,
                                QString *repository_p, QString *effectiveRepository_p, QString *branch_p, QString *path_p)
{
    QString svnprefix = pathName;
    svnprefix.truncate(rule.rx.matchedLength());

    if (svnprefix_p) {
        *svnprefix_p = svnprefix;
    }

    if (repository_p) {
        *repository_p = svnprefix;
        repository_p->replace(rule.rx, rule.repository);
        foreach (Rules::Match::Substitution subst, rule.repo_substs) {
            subst.apply(*repository_p);
        }
    }

    if (effectiveRepository_p) {
        *effectiveRepository_p = svnprefix;
        effectiveRepository_p->replace(rule.rx, rule.repository);
        foreach (Rules::Match::Substitution subst, rule.repo_substs) {
            subst.apply(*effectiveRepository_p);
        }
        Repository *repository = repositories.value(*effectiveRepository_p, 0);
        if (repository) {
            *effectiveRepository_p = repository->getEffectiveRepository()->getName();
        }
    }

    if (branch_p) {
        *branch_p = svnprefix;
        branch_p->replace(rule.rx, rule.branch);
        foreach (Rules::Match::Substitution subst, rule.branch_substs) {
            subst.apply(*branch_p);
        }
    }

    if (path_p) {
        QString prefix = svnprefix;
        prefix.replace(rule.rx, rule.prefix);
        QString suffix = pathName.mid(svnprefix.length());
        if (suffix.startsWith(rule.strip))
            suffix.replace(0, rule.strip.length(), "");
        *path_p = prefix + suffix;
    }
}

QString
SvnRevision::match_path_to_branch(const QString& path)
{
    QString branch;
    // There's really just 1 rule file ...
    foreach (const MatchRuleList matchRules, allMatchRules) {
        MatchRuleList::ConstIterator match = findMatchRule(matchRules, revnum, path);
        if (match != matchRules.constEnd()) {
            const Rules::Match &rule = *match;
            QString svnprefix, repository, effectiveRepository, prefix;
            switch (rule.action) {
                case Rules::Match::Export:
                case Rules::Match::Recurse:
                    splitPathName(rule, path, &svnprefix, &repository, &effectiveRepository, &branch, &prefix);
                    break;
                case Rules::Match::Ignore:
                    break;
                default:
                    qFatal("Rule match had unexpected action %d on %s", rule.action, qPrintable(path));
                    break;
            }
        }
    }
    return branch;
}


bool
SvnRevision::maybeParseSimpleMergeinfo(const int revnum, QList<mergeinfo>* mi_list)
{
    QProcess svn;
    // svn diff -c 179481 --properties-only file:///$PWD/base
    svn.start("svn",
            QStringList() << "diff"
            << "-c" << QString::number(revnum)
            << "--properties-only" << svn_repo_path);

    if (!svn.waitForFinished(-1)) {
        fprintf(stderr, "svn fork terminated abnormally for rev %d\n", revnum);
        exit(1);
    }

    const QString result = QString(svn.readAll()).remove("\\ No newline at end of property\n");

    // If there are N mergeinfo hits, and they all look like so, we have fully
    // empty mergeinfo and can skip this rev.
    // Added: svn:mergeinfo
    // ## -0,0 +0,0 ##
    // or
    // Deleted: svn:mergeinfo
    // ## -0,0 +0,0 ##
    // see r183713 for an interesting case. Maybe we should just delete all the
    // above strings?
    int del_mi = result.count(QRegularExpression(R"(^Deleted: svn:mergeinfo$)", QRegularExpression::MultilineOption));
    int add_mi = result.count(QRegularExpression(R"(^Added: svn:mergeinfo$)", QRegularExpression::MultilineOption));
    int diff_mi = result.count(QRegularExpression(R"(^Modified: svn:mergeinfo$)", QRegularExpression::MultilineOption));

    int del_mi_empty = result.count(QRegularExpression(R"(^Deleted: svn:mergeinfo
## -0,0 \+0,0 ##$)", QRegularExpression::MultilineOption));
    int add_mi_empty = result.count(QRegularExpression(R"(^Added: svn:mergeinfo
## -0,0 \+0,0 ##$)", QRegularExpression::MultilineOption));
    int diff_mi_empty = result.count(QRegularExpression(R"(^Modified: svn:mergeinfo
## -0,0 \+0,0 ##$)", QRegularExpression::MultilineOption));

    if ((del_mi+add_mi+diff_mi) == 0) {
        qFatal("Something went wrong parsing the mergeinfo!");
    }

    if ((del_mi+add_mi+diff_mi) > 0 && del_mi == del_mi_empty && add_mi == add_mi_empty && diff_mi == diff_mi_empty) {
        printf(" ===Skipping empty mergeinfo on %d=== ", revnum);
        return true;
    }

    if (del_mi >0 && add_mi == 0 && diff_mi == 0) {
        printf(" ===Skipping delete-only (%d) mergeinfo on %d=== ", del_mi, revnum);
        return true;
    }

    qDebug() << "=START=";
    qDebug() << qPrintable(result);
    qDebug() << "=END=";
    qDebug() << "mergeinfo parsing: del/add/mod=" << del_mi << del_mi_empty << add_mi << add_mi_empty << diff_mi << diff_mi_empty;
    QString tmp = result;

    // Remove property changes on files/paths that don't have any mergeinfo
    // changes in them.
    static QRegularExpression ignore = QRegularExpression(
           R"(Index: ([\S]+)
=============*
... ([\S]+).\([^)]+\)
... ([\S]+).\([^)]+\)

Property changes on: (?<path>[\S]+)
_____________*
((Added|Deleted|Modified): (fbsd|svn):(executable|n?o?keywords|notbinary|eol-style|mime-type)
## -[\d,]+ \+[\d,]+ ##
([-+].*){1,2}
*)+
(?=Index|$))");
    if (!ignore.isValid()) {
        qWarning() << "Error in regular expression" << ignore.errorString();
        exit(1);
    }
    QRegularExpressionMatchIterator i = ignore.globalMatch(result);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        qDebug() << "--- matched properties to ignore" << match.captured(0);
        tmp.remove(match.captured(0)); // eat the input
    }

    // Remove property changes on files/paths that deleted empty mergeinfo.
    // This happens about 300 times. (In fact, sometimes empty mergeinfo is
    // being added.)
    static QRegularExpression deletere = QRegularExpression(
           R"(Index: ([\S]+)
=============*
... ([\S]+).\([^)]+\)
... ([\S]+).\([^)]+\)

Property changes on: (?<path>[\S]+)
_____________*
(Added|Deleted): svn:mergeinfo
## -0,0 \+0,0 ##
*
(?=Index|$))");
    if (!deletere.isValid()) {
        qWarning() << "Error in regular expression" << deletere.errorString();
        exit(1);
    }
    i = deletere.globalMatch(tmp);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        qDebug() << "--- matched properties to ignore" << match.captured(0);
        tmp.remove(match.captured(0)); // eat the input
    }

    // NOTE: need to use a fully anchored match, otherwise e.g. r238926 gets
    // handled wrong, as it uses the first mergeinfo to deduce the merge-from,
    // which is incorrect and off-by-one! r240415 also merges up to 240357 but
    // ends up with 240326 instead. This reduces the "handled" mergeinfo from
    // 2000 out of 3000 down to 1125. The rest should be hard-coded.
    static QRegularExpression mire = QRegularExpression(
           R"((Index: ([\S]+)
=============*
... ([\S]+).\([^)]+\)
... ([\S]+).\([^)]+\)

Property changes on: (?<path>[\S]+)
_____________*
(?<garbage>(Added|Deleted|Modified): (fbsd|svn):(executable|n?o?keywords|eol-style|mime-type)
## -[\d,]+ \+[\d,]+ ##
([-+].*
){1,2})*)*(Modified|Added): svn:mergeinfo
## \-0,[01] \+0,[01] ##
   (?<dir>Merged|Reverse-merged) (?<from>[^:]+):r([0-9]*[-,])*(?<rev>[0-9]*)
*)");
    if (!mire.isValid()) {
        qWarning() << "Error in regular expression" << mire.errorString();
        exit(1);
    }
    i = mire.globalMatch(tmp);
    while (i.hasNext()) {
        mergeinfo mi;
        QRegularExpressionMatch match = i.next();
        if (match.captured(0).isEmpty() || match.captured(0) == "\n") {
            continue;
        }
        qDebug() << "Matched" <<  match.captured(0);
        if (match.captured("dir") == "Reverse-merged") {
            qDebug() << "=== Ignoring SVN rollbacks via mergeinfo";
            tmp.remove(match.captured(0)); // eat the input
            continue;  // parsed ok, but no action to take.
        }
        if (match.captured("dir") == "" && match.captured("garbage") != "") {
            qDebug() << "=== Ignoring garbage match";
            tmp.remove(match.captured(0)); // eat the input
            continue;
        }
        qDebug() << "=== Matched properties" <<  match.captured("garbage");
        qDebug() << "=== Matched path" <<  match.captured("path");
        qDebug() << "=== Matched dir" <<  match.captured("dir");
        qDebug() << "=== Matched from" <<  match.captured("from");
        qDebug() << "=== Matched rev" <<  match.captured("rev");
        QString f = "/" + match.captured("path") + "/";
        QString p = match.captured("from") + "/";  // Our rules expect a trailing '/'
        mi.rev = match.captured("rev").toInt(nullptr, 10);
        mi.from = match_path_to_branch(p);
        mi.to = match_path_to_branch(f);
        if (!mi.to.isEmpty() && !mi.from.isEmpty()) {
            qDebug() << "mergeinfo" << mi.from << mi.rev << "->" << mi.to;
            // Sometimes we get multiple pairs of from/to with different
            // revisions. Use the highest revision always.
            auto it = mi_list->begin();
            while (it != mi_list->end()) {
                if (it->from == mi.from && it->to == mi.to && it->rev < mi.rev) {
                    it = mi_list->erase(it);
                } else if (it->from == mi.from && it->to == mi.to && it->rev >= mi.rev) {
                    mi.from = "";
                    mi.to = "";
                } else {
                    ++it;
                }
            }
            if (!mi.to.isEmpty() && !mi.from.isEmpty()) {
                mi_list->push_back(mi);
            }
            tmp.remove(match.captured(0)); // eat the input
        } else {
            qDebug("Couldn't parse mergeinfo via rules file for %s or %s", qPrintable(p), qPrintable(f));
        }
    }
    std::sort(mi_list->begin(), mi_list->end());
    if (mi_list->size() == 1 && tmp == "") {
        return true;
    } else if (mi_list->size() > 1 && tmp == "") {
        // Special case the 66 cases where vendor/clang + vendor/llvm + lld,
        // openmp, etc. are merged in 1 rev. We want to properly record this,
        // but can't just do it for everything, as there are merges from head
        // into user/foo or project/bar that also copy a bunch of previously
        // recorded mergeinfo over.
        //
        // It should suffice to simply allow all set-merges as long as all
        // targets are the master branch, not projects or user or the like.
        // In fact, there are very few of those to master, most of them go into
        // projects/clangDDD-import.
        const bool all_master = std::all_of(mi_list->begin(), mi_list->end(),
                [](auto const& i) {return i.to == "master";});
        const bool all_clang_import = std::all_of(mi_list->begin(), mi_list->end(),
                [](auto const& i) {return i.to.startsWith("projects/clang") && i.to.endsWith("-import");});
        if (all_master || all_clang_import) {
            return true;
        }
    }
    if (mi_list->size() > 1) {
        qDebug() << "Got" << mi_list->size() << "different matches:" << *mi_list;
    }
    if (!tmp.isEmpty()) {
        qDebug() << "Remaining unparsed MI is" << qPrintable(tmp);
    }
    // We parsed everything, but it was probably an SVN rollback.
    if (tmp.isEmpty() && mi_list->isEmpty()) {
        return true;
    }

    QDir dir;
    if (dir.mkpath("mi")) {
        QProcess svn2;
        svn2.start("svn",
                QStringList() << "log"
                << "-vc" << QString::number(revnum)
                << svn_repo_path);

        if (!svn2.waitForFinished(-1)) {
            fprintf(stderr, "svn fork terminated abnormally for rev %d\n", revnum);
            exit(1);
        }
        const QString svn_log = QString(svn2.readAll());

        // This should create only about 3k files or so.
        QFile file(QString("mi/r%1.txt").arg(revnum));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << qPrintable(result);
            out << "\n";
            out << qPrintable(svn_log);
            foreach (mergeinfo mi, *mi_list) {
                out << "\n { " + QString::number(revnum) + ", { " << mi << " } },";
            }
            out << "\n";
        }
    }
    mi_list->clear();
    return false;
}

int SvnRevision::prepareTransactions()
{
    // find out what was changed in this revision:
    apr_hash_t *changes;
    SVN_ERR(svn_fs_paths_changed2(&changes, fs_root, pool));

    QMap<QByteArray, svn_fs_path_change2_t*> map;
    for (apr_hash_index_t *i = apr_hash_first(pool, changes); i; i = apr_hash_next(i)) {
        const void *vkey;
        void *value;
        apr_hash_this(i, &vkey, NULL, &value);
        const char *key = reinterpret_cast<const char *>(vkey);
        svn_fs_path_change2_t *change = reinterpret_cast<svn_fs_path_change2_t *>(value);
        // If we mix path deletions with path adds/replaces we might erase a
        // branch after that it has been reset -> history truncated
        if (map.contains(QByteArray(key))) {
            // If the same path is deleted and added, we need to put the
            // deletions into the map first, then the addition.
            if (change->change_kind == svn_fs_path_change_delete) {
                // XXX
            }
            fprintf(stderr, "\nDuplicate key found in rev %d: %s\n", revnum, key);
            fprintf(stderr, "This needs more code to be handled, file a bug report\n");
            fflush(stderr);
            exit(1);
        }
        map.insertMulti(QByteArray(key), change);
    }

    QMapIterator<QByteArray, svn_fs_path_change2_t*> i(map);
    bool mergeinfo_found = false;
    while (i.hasNext()) {
        i.next();
        if (i.value()->mergeinfo_mod == svn_tristate_true) {
            mergeinfo_found = true;
        }
        if (exportEntry(i.key(), i.value(), changes) == EXIT_FAILURE)
            return EXIT_FAILURE;
    }

    // Handle the deletions and renames that we collected throughout the
    // path/rule matching and issue them once.
    for (const auto& rbp : deletions_.toStdMap()) {
        const QString& repo_branch = rbp.first;
        // NOTE: must exist, for now. might have to relax this requirement ...
        Repository::Transaction *txn = transactions[repo_branch];
        for (const auto& path : rbp.second) {
            if(ruledebug)
                qDebug() << "delete (" << txn->getBranch() << path << ")";
            txn->deleteFile(path);
        }
    }
    for (const auto& rbp : renames_.toStdMap()) {
        const QString& repo_branch = rbp.first;
        // NOTE: must exist, for now. might have to relax this requirement ...
        Repository::Transaction *txn = transactions[repo_branch];
        for (const auto& from_to : rbp.second.toStdMap()) {
            if(ruledebug)
              qDebug() << "rename (" << txn->getBranch() << from_to.first
                       << "->" << from_to.second << ")";
            txn->renameFile(from_to.first, from_to.second);
        }
    }

    // Not in src or in the pre-svn days, skip.
    if (!svn_repo_path.endsWith("base") || revnum < 179447)
        return EXIT_SUCCESS;

    static const QString repository = "freebsd-base.git";
    // Force a bunch of merges, even though SVN never properly recorded them.
    // Some of them were later added via svn:mergeinfo. Even more had changes
    // imported into head, then later the vendor import was done (?!) and
    // finally the mergeinfo recorded. We're not going to patch that up ...
    static const QMap<int, mergeinfo> force_merges = {
        // Recorded in r265214
        { 264691, { .from = "vendor/openssh/dist", .rev = 264690, .to = "master" } },
        // Recorded in r299540
        { 299540, { .from = "vendor/libarchive/dist", .rev = 299539, .to = "master" } },
        // This was actually merged from master into vendor
        { 317396, { .from = "vendor/less/dist", .rev = 317395, .to = "master" } },
        // Recorded in r333678
        { 333677, { .from = "vendor/openssh/dist", .rev = 333676, .to = "master" } },
    };
    if (force_merges.contains(revnum)) {
        const auto& mi = force_merges.value(revnum);
        static const QString svnprefix = "";
        const QString& branch = mi.to;
        Repository::Transaction *txn = transactions.value(repository + branch, 0);
        if (!txn) {
            Repository *repo = repositories.value(repository, 0);
            txn = repo->newTransaction(branch, svnprefix, revnum);
            if (!txn)
                return EXIT_FAILURE;
            transactions.insert(repository + branch, txn);
        }
        txn->noteCopyFromBranch(mi.from, mi.rev);
        needCommit = true;
        return EXIT_SUCCESS;
    }

    // No svn:mergeinfo found
    if (!mergeinfo_found)
        return EXIT_SUCCESS;

    // List of revisions to skip as their mergeinfo is complex and irrelevant in
    // terms of git.
    static const QSet<int> skip_mergeinfo = {
        196075, 179468, 244485, 244487, 262833, 262834, 355814, 193205, 253716,
        184527, 268229,
        // self-referential mergeinfo
        180475, 181836, 181837, 183229, 286109, 288439, 228777, 228776,
        // These are branch creations or head → project IFCs where a whole
        // bunch of mergeinfo was copied over, there's nothing to do for these.
        // NOTE: these are sorted by the size of the resulting svn diff output.
        // They copy around endless mergeinfo for every frigging file.
        319809, 210031, 219265, 197923, 218442, 222000, 219262, 210035, 232557,
        218520, 219313, 219314, 218579, 218451, 218462, 219259, 197919, 302021,
        232280, 218437, 197927, 217943, 210037, 188064, 216688, 191131,
        // Stuff got moved around, yo.
        277786, 188942,
        // These are predominantly mergeinfo bootstraps, deletes, fixups,
        // records-after-the-fact and a whole bunch more.
        186082, 188940, 188955, 189585, 189587, 189613, 190749, 191931,
        179481, 179511, 179512, 179601, 179683, 179684, 179698, 179982, 179997,
        180006, 180007, 180241, 180243, 180244, 180245, 180402, 180457, 180472,
        180764, 181081, 181290, 181372, 181373, 181378, 181379, 181380, 181408,
        181415, 181484, 181519, 181541, 181633, 181650, 181705, 181711, 181712,
        181715, 181716, 181723, 181725, 181728, 181829, 182049, 182050, 182232,
        182233, 182244, 182246, 182306, 182316, 182317, 182318, 182319, 182320,
        182327, 182328, 182335, 182347, 182508, 182509, 182510, 182511, 182514,
        182581, 182597, 182598, 182604, 182610, 182611, 182612, 182613, 182623,
        182701, 182705, 182770, 182772, 183227, 183395, 183404, 183405, 183434,
        183715, 183716, 183722, 183956, 184305, 184306, 184637, 184788, 184901,
        184929, 184930, 184931, 184940, 185340, 185351, 185613, 185615, 185709,
        185875, 186066, 186232, 186268, 186991, 186992, 186998, 187049, 187266,
        187268, 187448, 187625, 187912, 188283, 188436, 189257, 189266, 190019,
        190155, 190156, 190205, 190258, 190326, 190812, 191926, 192388, 193523,
        193743, 193749, 193915, 193946, 194010, 194341, 194347, 194377, 194453,
        194603, 195588, 195823, 195835, 196324, 196325, 196327, 196329, 196343,
        196606, 197068, 197069, 197232, 197233, 197329, 197353, 197378, 197739,
        197792, 197839, 197846, 197941, 198516, 198517, 199030, 199377, 199406,
        199973, 200367, 200454, 200575, 201365, 201635, 201826, 201828, 201829,
        201939, 202354, 202936, 202938, 202949, 202952, 203039, 203117, 203389,
        203753, 204097, 204239, 204930, 205483, 205484, 205565, 205684, 205685,
        205692, 205696, 205697, 205703, 205704, 206096, 206101, 206201, 206531,
        206564, 206750, 207003, 208215, 208216, 208246, 208306, 208399, 208401,
        208521, 208956, 209137, 209479, 209480, 210147, 210223, 210489, 211403,
        211492, 211662, 211703, 211942, 211943, 211961, 212869, 212871, 213352,
        213355, 213989, 214344, 215479, 215681, 215682, 215972, 215994, 216459,
        216460, 216601, 217340, 217448, 217901, 217983, 219549, 219734, 220492,
        220493, 220957, 221767, 222565, 223301, 223549, 224146, 224288, 224530,
        225867, 226051, 227624, 227625, 229557, 230240, 230241, 231152, 231155,
        231365, 231366, 231856, 231974, 232294, 232622, 235249, 235250, 235665,
        235763, 235784, 235794, 236203, 236721, 236732, 237954, 237971, 238401,
        238684, 239526, 240553, 240911, 241450, 244884, 244885, 244886, 244887,
        244888, 244889, 245826, 245827, 245855, 245856, 246409, 246411, 247017,
        247464, 248086, 248238, 249364, 249469, 250347, 251917, 252024, 252137,
        253482, 253961, 254014, 254099, 254113, 254119, 254213, 254972, 255831,
        256099, 256344, 258405, 258562, 258804, 259334, 259445, 259485, 259488,
        260658, 260677, 261264, 261317, 262045, 262050, 262223, 262342, 262637,
        262792, 262875, 263707, 263888, 264249, 265214, 265293, 265294, 265645,
        265991, 265992, 265997, 266027, 266389, 266767, 266768, 266840, 267410,
        267414, 267455, 267456, 267539, 267684, 267702, 267939, 268124, 268213,
        268343, 268627, 270314, 271741, 272462, 272837, 273497, 274719, 275188,
        275323, 275580, 276366, 276369, 276676, 278157, 280465, 281279, 282058,
        282060, 282068, 282479, 282993, 283085, 283154, 283798, 283862, 284298,
        284299, 284300, 284305, 284307, 284715, 284973, 284974, 285832, 285969,
        286679, 287018, 287630, 287660, 287743, 287938, 288034, 288328, 288331,
        288928, 289176, 289179, 289235, 289237, 289301, 289690, 289827, 291187,
        291803, 292505, 292912, 293114, 293122, 293149, 293150, 293152, 293199,
        293207, 293211, 293417, 293645, 294168, 294376, 295411, 295434, 295446,
        296966, 298096, 298097, 298098, 298099, 298623, 299540, 301078, 301501,
        301813, 301831, 301832, 302009, 302161, 303162, 303363, 304419, 304661,
        305204, 305817, 305818, 305892, 305893, 306079, 309094, 309107, 309169,
        309535, 310001, 312217, 312640, 313435, 313470, 313484, 313503, 313534,
        313678, 314108, 314284, 314285, 314941, 315061, 315414, 316086, 317396,
        317852, 318195, 318248, 318735, 318834, 318922, 318926, 320562, 320563,
        320995, 321215, 321216, 321254, 321255, 321278, 321280, 321352, 321557,
        321560, 321564, 322220, 322247, 323719, 324298, 325476, 327414, 327634,
        327887, 327933, 328105, 328602, 328854, 328859, 328870, 329151, 329871,
        330327, 331027, 331110, 331336, 331494, 331685, 331796, 332051, 332129,
        332366, 333678, 333764, 335167, 335695, 335758, 336341, 336342, 336502,
        336510, 336512, 336513, 336515, 336939, 337011, 337019, 337095, 337111,
        337188, 337311, 337312, 337314, 337954, 337955, 339018, 339156, 339253,
        339257, 339258, 339617, 341000, 341597, 342951, 343243, 343317, 343318,
        344130, 344284, 344287, 344414, 344778, 345232, 345716, 348038, 348046,
        348723, 349702, 351342, 351392, 352043, 352239, 352254, 352346, 352347,
        352770, 352771, 353352, 353567, 353973, 353974, 353996, 355292, 355903,
        355948, 356095, 356774, 356930, 357060, 357584, 358850, 360666, 360668,
        // flotsam and jetsam, often simple file renames by necessity need to copy the mergeinfo around :/
        184541, 185386, 185429, 185578, 185632, 185633, 185919, 185633, 185637,
        186441, 187190, 187620, 188544, 188633, 188939, 188998, 189361, 190581,
        190633, 191984, 192744, 193441, 193532, 194340, 195124, 201980, 202056,
        204857, 205081, 207951, 209161, 210709, 210710, 212035, 225520, 226780,
        228773, 228775, 230184, 231853, 240786, 241808, 242451, 242487, 243600,
        243600, 252125, 254305, 256861, 262606, 281994, 281996, 287043, 289134,
        289151, 289158, 289172, 291115, 295380, 298095, 298396, 306097, 320515,
        330503, 344427, 345068, 355951,
        // These are partial branch copies, some of them are actually ok, like
        // the ones from vendor into vendor.
        180458, 191957, 200812, 201537, 204479, 205302, 206926, 208122, 209627,
        210708, 217338, 221956, 223144, 227632, 227670, 234080, 234647, 238053,
        244587, 256282, 259687, 266652, 276329, 277620, 278877, 280454, 287632,
        296049, 296962, 300828, 328072,
    };
    if (skip_mergeinfo.contains(revnum)) {
        return EXIT_SUCCESS;
    }

    // List of revisions to skip as their mergeinfo is empty and consists of
    // -0,0 +0,0 changes only. We hardcode the list here as it a) never changes
    // and so we can b) skip a whole lot of forking into svn(1).
    static const QSet<int> empty_mergeinfo = {
        179566, 179790, 180332, 181027, 181074, 181522, 181524, 181601, 181738,
        181739, 181740, 181741, 181872, 181905, 182044, 182326, 182724, 183198,
        183226, 183430, 183431, 183432, 183433, 183654, 183714, 183910, 184330,
        184425, 184521, 184562, 185160, 185305, 185307, 185402, 185539, 185626,
        185631, 186256, 186261, 186535, 186934, 187064, 187220, 187258, 187962,
        189628, 189705, 193734, 194143, 194148, 194150, 194153, 194155, 194157,
        194159, 194674, 196280, 196696, 197352, 197509, 198498, 203325, 205176,
        282800, 282913, 283041, 283078, 283177, 283180, 283608, 283619, 283621,
        283626, 283628, 284185, 284187, 284235, 284244, 284396, 284678, 284680,
        284992, 285102, 285164, 285166, 285194, 285210, 286426, 286428, 286498,
        286500, 286502, 286508, 286749, 287451, 287504, 287511, 287513, 287515,
        287517, 287519, 287629, 287916, 288141, 288150, 288244, 289062, 289069,
        289285, 289721, 290005,
        // These are delete-only mergeinfos, usually deleting Reverse-merged
        // mergeinfo, whatever that is.
        190634, 194300, 196219, 196322, 196330, 196698, 198048, 198052, 198057,
        198058, 199096, 199141, 202103, 202105, 253750, 259935, 261839, 262486,
        276402, 293215, 298094, 337607, 349592,
        // Other mergeinfo that copies everything from head around
        296962,
    };
    if (empty_mergeinfo.contains(revnum)) {
        return EXIT_SUCCESS;
    }

    // Apparently, we already recorded some form of merge, could be to a
    // different branch though.
    if (merge_from_branch_ != "" && merge_from_rev_ != "")
        return EXIT_SUCCESS;

    // We don't "merge" into stable branches, so silently skip all those.
    QStringList branches;
    bool non_stable = false;
    foreach (const QString &value, to_branches_) {
        if (!value.startsWith("stable/") && !value.startsWith("releng/")) {
            non_stable = true;
        }
        branches << value;
    }
    if (to_branches_.size() > 0 && !non_stable)
        return EXIT_SUCCESS;

    printf(" MERGEINFO: rev %d has pure mergeinfo w/o path copies going into %d branches: %s",
           revnum, to_branches_.size(), qPrintable(branches.join(" ")));
    fflush(stdout);

    if (to_branches_.size() == 0) {
        printf(" MONKEYMERGE don't know how to handle empty branches!");
        return EXIT_SUCCESS;
    }

    // Things we patch up manually as the SVN history around them is ...
    // creative.
    static const QMultiMap<int, mergeinfo> manual_merges = {
        // merges from vendor/tzdata/dist *and* vendor/tzdata (sic!)
        { 181413, { .from = "vendor/tzdata/dist", .rev = 181403, .to = "master" } },
        { 182352, { .from = "vendor/sendmail/dist", .rev = 182351, .to = "master" } },
        { 184966, { .from = "stable/7", .rev = 184965, .to = "user/dfr/gssapi/7" } },
        { 184968, { .from = "stable/6", .rev = 184967, .to = "user/dfr/gssapi/6" } },
        { 185198, { .from = "master", .rev = 185082, .to = "projects/releng_7_xen" } },
        { 185590, { .from = "stable/7", .rev = 185562, .to = "projects/releng_7_xen" } },
        { 185850, { .from = "stable/7", .rev = 185849, .to = "projects/releng_7_xen" } },
        { 185863, { .from = "stable/7", .rev = 185862, .to = "user/dfr/gssapi/7" } },
        { 185865, { .from = "stable/6", .rev = 185863, .to = "user/dfr/gssapi/6" } },
        { 186421, { .from = "stable/7", .rev = 186420, .to = "projects/releng_7_xen" } },
        { 186989, { .from = "stable/7", .rev = 186962, .to = "projects/vap7" } },
        { 187111, { .from = "stable/7", .rev = 187108, .to = "projects/vap7" } },
        { 187115, { .from = "stable/7", .rev = 187106, .to = "projects/vap7" } },
        { 187303, { .from = "master", .rev = 187302, .to = "user/thompsa/usb" } },
        { 187503, { .from = "master", .rev = 187342, .to = "user/sam/wifi" } },
        { 187745, { .from = "master", .rev = 187743, .to = "user/sam/wifi" } },
        { 188430, { .from = "stable/7", .rev = 188429, .to = "projects/tcp_cc_7.x" } },
        { 188542, { .from = "master", .rev = 180140, .to = "projects/vap7" } },
        { 189614, { .from = "user/dfr/xenhvm/6", .rev = 189451, .to = "user/dfr/xenhvm/7" } },
        // has a bogus path
        { 189618, { .from = "vendor/top/dist", .rev = 183430, .to = "master" } },
        { 190861, { .from = "stable/7", .rev = 190860, .to = "projects/tcp_cc_7.x" } },
        { 190952, { .from = "master", .rev = 190951, .to = "projects/tcp_cc_8.x" } },
        { 192480, { .from = "master", .rev = 192361, .to = "user/kmacy/ZFS_MFC" } },
        { 194455, { .from = "master", .rev = 194453, .to = "user/gnn/fasttrap" } },
        { 195123, { .from = "user/kmacy/releng_7_2_fcs", .rev = 194517, .to = "user/kmacy/head_ppacket" } },
        { 197858, { .from = "stable/8", .rev = 197854, .to = "projects/capabilities8" } },
        { 198336, { .from = "stable/8", .rev = 198334, .to = "projects/capabilities8" } },
        // has 3x merges from different subdirs
        { 200832, { .from = "vendor/tzcode/dist", .rev = 200830, .to = "master" } },
        { 201785, { .from = "stable/8", .rev = 201781, .to = "projects/capabilities8" } },
        { 203702, { .from = "master", .rev = 200930, .to = "user/eri/pf45" } },
        // has a bunch of schmutz
        { 204934, { .from = "vendor/x86emu/dist", .rev = 204933, .to = "master" } },
        { 205644, { .from = "master", .rev = 205643, .to = "projects/ppc64" } },
        { 205719, { .from = "master", .rev = 205718, .to = "user/jmallett/octeon" } },
        { 206719, { .from = "master", .rev = 206718, .to = "user/jmallett/octeon" } },
        { 207810, { .from = "master", .rev = 184124, .to = "user/imp/masq" } },
        { 209629, { .from = "projects/ppc64", .rev = 209628, .to = "user/nwhitehorn/ps3" } },
        { 211724, { .from = "master", .rev = 211721, .to = "user/edwin/calendar" } },
        { 213534, { .from = "vendor/llvm/dist", .rev = 213533, .to = "master" } },
        { 213534, { .from = "vendor/clang/dist", .rev = 213533, .to = "master" } },
        // merged vendor/ee/dist *and* vendor/ee/1.5.2
        { 213567, { .from = "vendor/ee/dist", .rev = 213565, .to = "master" } },
        { 216020, { .from = "stable/6", .rev = 216017, .to = "projects/releng_6_xen" } },
        { 216270, { .from = "stable/7", .rev = 216269, .to = "projects/stable_7_xen" } },
        { 217945, { .from = "stable/7", .rev = 217943, .to = "projects/graid/7" } },
        { 218876, { .from = "master", .rev = 218875, .to = "projects/altix" } },
        { 219743, { .from = "master", .rev = 219742, .to = "projects/altix" } },
        { 220150, { .from = "vendor/gcc/dist", .rev = 219451, .to = "master" } },
        { 221399, { .from = "master", .rev = 221396, .to = "projects/largeSMP" } },
        { 221562, { .from = "master", .rev = 221557, .to = "projects/largeSMP" } },
        { 221717, { .from = "master", .rev = 221716, .to = "projects/largeSMP" } },
        { 221872, { .from = "master", .rev = 221870, .to = "projects/largeSMP" } },
        { 222548, { .from = "master", .rev = 222547, .to = "projects/largeSMP" } },
        { 222862, { .from = "master", .rev = 222861, .to = "user/hrs/ipv6" } },
        { 223758, { .from = "projects/largeSMP", .rev = 223757, .to = "master" } },
        { 225524, { .from = "vendor/openresolv/dist", .rev = 225523, .to = "master" } },
        { 225870, { .from = "master", .rev = 225869, .to = "user/attilio/vmcontention" } },
        { 226982, { .from = "master", .rev = 226981, .to = "user/attilio/vmcontention" } },
        { 229307, { .from = "releng/9.0", .rev = 229306, .to = "refs/tags/release/9.0.0" } },
        // merged /dist and tag
        { 229413, { .from = "vendor/compiler-rt/dist", .rev = 229411, .to = "master" } },
        { 231852, { .from = "projects/multi-fibv6/head", .rev = 231848, .to = "master" } },
        { 231877, { .from = "stable/8", .rev = 231871, .to = "projects/multi-fibv6/8" } },
        { 232293, { .from = "stable/8", .rev = 232292, .to = "projects/multi-fibv6/8" } },
        { 232555, { .from = "stable/9", .rev = 232554, .to = "projects/multi-fibv6/9" } },
        { 232993, { .from = "stable/7", .rev = 232985, .to = "projects/multi-fibv6/7" } },
        { 233959, { .from = "master", .rev = 233958, .to = "user/attilio/vmcontention" } },
        { 238268, { .from = "master", .rev = 238266, .to = "user/attilio/vmc-playground" } },
        { 238422, { .from = "vendor/illumos/dist", .rev = 238417, .to = "master" } },
        { 240141, { .from = "vendor/atf/dist", .rev = 240140, .to = "master" } },
        { 240517, { .from = "vendor/byacc/dist", .rev = 240505, .to = "master" } },
        { 241973, { .from = "vendor/acpica/dist", .rev = 241747, .to = "master" } },
        { 242102, { .from = "vendor/NetBSD/bmake/dist", .rev = 242094, .to = "master" } },
        { 242150, { .from = "master", .rev = 242149, .to = "user/andre/tcp_workqueue" } },
        { 242513, { .from = "master", .rev = 242511, .to = "projects/efika_mx" } },
        { 242912, { .from = "master", .rev = 242911, .to = "user/andre/tcp_workqueue" } },
        { 246444, { .from = "user/attilio/vmcontention", .rev = 246443, .to = "user/attilio/vmc-playground" } },
        { 246449, { .from = "master", .rev = 242525, .to = "projects/bmake" } },
        { 246457, { .from = "user/attilio/vmcontention", .rev = 246456, .to = "user/attilio/vmc-playground" } },
        { 246481, { .from = "user/attilio/vmcontention", .rev = 246480, .to = "user/attilio/vmc-playground" } },
        { 246572, { .from = "master", .rev = 246571, .to = "user/attilio/vmcontention" } },
        { 246573, { .from = "user/attilio/vmcontention", .rev = 246572, .to = "user/attilio/vmc-playground" } },
        { 246639, { .from = "master", .rev = 246638, .to = "user/attilio/vmcontention" } },
        { 246640, { .from = "user/attilio/vmcontention", .rev = 246639, .to = "user/attilio/vmc-playground" } },
        { 247354, { .from = "user/attilio/vmcontention", .rev = 247352, .to = "user/attilio/vmc-playground" } },
        { 247362, { .from = "user/attilio/vmcontention", .rev = 247361, .to = "user/attilio/vmc-playground" } },
        { 247401, { .from = "master", .rev = 247400, .to = "user/attilio/vmobj-rwlock" } },
        { 247402, { .from = "master", .rev = 247401, .to = "user/attilio/vmcontention" } },
        { 247403, { .from = "user/attilio/vmcontention", .rev = 247402, .to = "user/attilio/vmc-playground" } },
        { 248191, { .from = "master", .rev = 248189, .to = "user/attilio/vmobj-readlock" } },
        { 248206, { .from = "user/attilio/vmcontention", .rev = 248205, .to = "user/attilio/vmobj-readlock" } },
        { 248229, { .from = "user/attilio/vmcontention", .rev = 248228, .to = "user/attilio/vmobj-readlock" } },
        { 248302, { .from = "vendor/NetBSD/libc-vis/dist", .rev = 247132, .to = "master" } },
        { 249190, { .from = "master", .rev = 249189, .to = "projects/counters" } },
        { 249280, { .from = "master", .rev = 249278, .to = "user/attilio/vmobj-readlock" } },
        { 249482, { .from = "master", .rev = 249481, .to = "projects/camlock" } },
        { 249706, { .from = "master", .rev = 249704, .to = "user/attilio/jeff-numa" } },
        { 250621, { .from = "vendor/flex/dist", .rev = 250620, .to = "projects/flex-sf" } },
        { 250631, { .from = "master", .rev = 250630, .to = "user/adrian/net80211_tx" } },
        { 250934, { .from = "user/attilio/vmcontention", .rev = 250933, .to = "user/attilio/vmobj-readlock" } },
        { 251062, { .from = "user/attilio/vmcontention", .rev = 251060, .to = "user/attilio/vmobj-readlock" } },
        { 251082, { .from = "master", .rev = 251079, .to = "user/attilio/vmobj-readlock" } },
        { 251956, { .from = "vendor/subversion/dist", .rev = 251955, .to = "master" } },
        { 252030, { .from = "master", .rev = 252028, .to = "user/attilio/vmobj-readlock" } },
        { 253942, { .from = "master", .rev = 253940, .to = "user/attilio/vmobj-readlock" } },
        { 253943, { .from = "master", .rev = 253940, .to = "user/attilio/vmobj-fullread" } },
        { 253955, { .from = "master", .rev = 253953, .to = "user/attilio/vmobj-readlock" } },
        { 254110, { .from = "master", .rev = 254109, .to = "user/attilio/vmcontention" } },
        { 254111, { .from = "master", .rev = 254109, .to = "user/attilio/vmobj-readlock" } },
        { 254145, { .from = "master", .rev = 254141, .to = "user/attilio/vmobj-readlock" } },
        { 254188, { .from = "master", .rev = 254185, .to = "user/attilio/vmobj-readlock" } },
        { 254635, { .from = "master", .rev = 254081, .to = "projects/bhyve_npt_pmap" } },
        { 254788, { .from = "master", .rev = 254787, .to = "projects/camlock" } },
        { 255033, { .from = "vendor/NetBSD/libexecinfo/dist", .rev = 255026, .to = "master" } },
        { 261431, { .from = "vendor/libyaml/dist", .rev = 261430, .to = "master" } },
        { 265153, { .from = "master", .rev = 265152, .to = "projects/random_number_generator" } },
        { 265226, { .from = "master", .rev = 265225, .to = "projects/random_number_generator" } },
        { 268482, { .from = "master", .rev = 268481, .to = "projects/random_number_generator" } },
        { 269851, { .from = "vendor/sqlite3/dist", .rev = 269850, .to = "master" } },
        { 274350, { .from = "master", .rev = 274349, .to = "projects/ifnet" } },
        { 275327, { .from = "master", .rev = 275326, .to = "projects/sendfile" } },
        { 275364, { .from = "master", .rev = 275363, .to = "projects/clang350-import" } },
        { 276641, { .from = "vendor/NetBSD/vis/dist", .rev = 276640, .to = "master" } },
        { 277809, { .from = "master", .rev = 277803, .to = "projects/clang360-import" } },
        { 277869, { .from = "vendor/amd/dist", .rev = 277866, .to = "master" } },
        { 277896, { .from = "master", .rev = 277895, .to = "projects/clang360-import" } },
        { 277984, { .from = "master", .rev = 277983, .to = "user/dchagin/lemul" } },
        { 277999, { .from = "master", .rev = 277998, .to = "projects/clang360-import" } },
        { 279575, { .from = "master", .rev = 279574, .to = "user/marcel/libvdsk" } },
        { 279717, { .from = "master", .rev = 279716, .to = "projects/cxl_iscsi" } },
        { 280030, { .from = "master", .rev = 280029, .to = "projects/clang360-import" } },
        { 282069, { .from = "master", .rev = 282068, .to = "user/ngie/more-tests" } },
        { 286694, { .from = "master", .rev = 286693, .to = "projects/clang-trunk" } },
        { 287722, { .from = "master", .rev = 287721, .to = "projects/iosched" } },
        { 288329, { .from = "master", .rev = 288328, .to = "user/ngie/more-tests" } },
        { 288684, { .from = "master", .rev = 288683, .to = "user/ngie/more-tests" } },
        { 288955, { .from = "master", .rev = 288954, .to = "user/ngie/more-tests2" } },
        { 289123, { .from = "vendor/dma/dist", .rev = 289122, .to = "master" } },
        { 289133, { .from = "master", .rev = 289132, .to = "user/ngie/more-tests2" } },
        { 289135, { .from = "master", .rev = 289134, .to = "user/ngie/more-tests2" } },
        { 292630, { .from = "stable/10", .rev = 292629, .to = "user/ngie/stable-10-libnv" } },
        { 292857, { .from = "stable/10", .rev = 292856, .to = "user/ngie/stable-10-libnv" } },
        { 292886, { .from = "stable/10", .rev = 292885, .to = "user/ngie/stable-10-libnv" } },
        { 292909, { .from = "stable/10", .rev = 292908, .to = "user/ngie/stable-10-libnv" } },
        { 292974, { .from = "stable/10", .rev = 292973, .to = "user/ngie/stable-10-libnv" } },
        { 292977, { .from = "stable/10", .rev = 292976, .to = "user/ngie/stable-10-libnv" } },
        { 293113, { .from = "stable/10", .rev = 293112, .to = "user/ngie/stable-10-libnv" } },
        { 293136, { .from = "stable/10", .rev = 293135, .to = "user/ngie/stable-10-libnv" } },
        { 293618, { .from = "stable/10", .rev = 293617, .to = "user/ngie/stable-10-libnv" } },
        { 293638, { .from = "stable/10", .rev = 293637, .to = "user/ngie/stable-10-fix-LINT-NOINET" } },
        { 293639, { .from = "stable/10", .rev = 293638, .to = "user/ngie/stable-10-libnv" } },
        { 293717, { .from = "stable/10", .rev = 293716, .to = "user/ngie/stable-10-libnv" } },
        { 293780, { .from = "stable/10", .rev = 293779, .to = "user/ngie/stable-10-libnv" } },
        { 294069, { .from = "stable/10", .rev = 294068, .to = "user/ngie/stable-10-libnv" } },
        { 294085, { .from = "stable/10", .rev = 294084, .to = "user/ngie/stable-10-libnv" } },
        { 294236, { .from = "stable/10", .rev = 294235, .to = "user/ngie/stable-10-libnv" } },
        { 294513, { .from = "stable/10", .rev = 294512, .to = "user/ngie/stable-10-libnv" } },
        { 294659, { .from = "stable/10", .rev = 294658, .to = "user/ngie/stable-10-libnv" } },
        { 295369, { .from = "vendor/NetBSD/libedit/dist", .rev = 295360, .to = "master" } },
        { 297333, { .from = "stable/10", .rev = 297332, .to = "user/ngie/stable-10-libnv" } },
        { 297861, { .from = "master", .rev = 297860, .to = "projects/release-pkg" } },
        { 299896, { .from = "vendor/libarchive/dist", .rev = 299895, .to = "master" } },
        { 301759, { .from = "vendor/ldns-host/dist", .rev = 301755, .to = "master" } },
        { 302299, { .from = "master", .rev = 302298, .to = "projects/vnet" } },
        { 302321, { .from = "vendor/Juniper/libxo/dist", .rev = 302314, .to = "master" } },
        { 302844, { .from = "stable/10", .rev = 302843, .to = "user/ngie/stable-10-libnv" } },
        { 303084, { .from = "vendor/illumos/dist", .rev = 303082, .to = "master" } },
        { 309170, { .from = "master", .rev = 309169, .to = "projects/clang391-import" } },
        { 309175, { .from = "vendor/clang/dist", .rev = 309170, .to = "projects/clang391-import" } },
        { 309175, { .from = "vendor/lld/dist", .rev = 309170, .to = "projects/clang391-import" } },
        { 309175, { .from = "vendor/lldb/dist", .rev = 309170, .to = "projects/clang391-import" } },
        { 309175, { .from = "vendor/llvm/dist", .rev = 309170, .to = "projects/clang391-import" } },
        { 312997, { .from = "vendor/NetBSD/libedit/dist", .rev = 296159, .to = "master" } },
        { 313501, { .from = "stable/10", .rev = 313500, .to = "projects/stable-10-backport-test-changes" } },
        { 314523, { .from = "master", .rev = 314522, .to = "projects/clang400-import" } },
        { 317802, { .from = "vendor/NetBSD/blacklist/dist", .rev = 302314, .to = "master" } },
        { 318484, { .from = "stable/11", .rev = 318483, .to = "user/markj/PQ_LAUNDRY_11" } },
        { 318691, { .from = "stable/11", .rev = 318690, .to = "user/markj/PQ_LAUNDRY_11" } },
        { 320994, { .from = "master", .rev = 320993, .to = "projects/clang500-import" } },
        { 321864, { .from = "vendor-sys/ena-com/dist", .rev = 321861, .to = "master" } },
        { 328777, { .from = "master", .rev = 328776, .to = "user/markj/netdump" } },
        { 331318, { .from = "master", .rev = 323082, .to = "projects/bsd_rdma_4_9_stable_11" } },
        { 337309, { .from = "vendor/llvm/dist-release_70", .rev = 337308, .to = "projects/clang700-import" } },
        { 337310, { .from = "vendor/clang/dist-release_70", .rev = 337309, .to = "projects/clang700-import" } },
        { 337313, { .from = "vendor/compiler-rt/dist-release_70", .rev = 337312, .to = "projects/clang700-import" } },
        { 344951, { .from = "vendor/clang/dist-release_80", .rev = 344949, .to = "master" } },
        { 344951, { .from = "vendor/compiler-rt/dist-release_80", .rev = 344950, .to = "master" } },
        { 344951, { .from = "vendor/libc++/dist-release_80", .rev = 344950, .to = "master" } },
        { 344951, { .from = "vendor/lld/dist-release_80", .rev = 344950, .to = "master" } },
        { 344951, { .from = "vendor/lldb/dist-release_80", .rev = 344950, .to = "master" } },
        { 344951, { .from = "vendor/llvm/dist-release_80", .rev = 344949, .to = "master" } },
        { 345725, { .from = "master", .rev = 345724, .to = "projects/capsicum-test" } },
        { 351708, { .from = "vendor/compiler-rt/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/libc++/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/llvm-libunwind/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/clang/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/lld/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/lld/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/lldb/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/llvm/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 351708, { .from = "vendor/llvm-openmp/dist-release_90", .rev = 351683, .to = "projects/clang900-import" } },
        { 355957, { .from = "vendor/llvm-project/master", .rev = 355956, .to = "master" } },
        { 356931, { .from = "master", .rev = 357178, .to = "projects/clang1000-import" } },
        { 357179, { .from = "master", .rev = 357178, .to = "projects/clang1000-import" } },
        // These 2 were merged from "/vendor" (sic! no subdir)
        { 357636, { .from = "vendor/NetBSD/tests/dist", .rev = 357635, .to = "master" } },
        { 357688, { .from = "vendor/NetBSD/tests/dist", .rev = 357687, .to = "master" } },
        { 357966, { .from = "master", .rev = 357965, .to = "projects/clang1000-import" } },
        { 358131, { .from = "master", .rev = 358130, .to = "projects/clang1000-import" } },
        // I have no words ...
        { 361802, { .from = "vendor/edk2/dist", .rev = 361765, .to = "master" } },
    };

    bool parse_ok = false;
    QList<mergeinfo> mi;
    if (manual_merges.contains(revnum)) {
        mi = manual_merges.values(revnum);
        parse_ok = true;
    } else {
        // There are quite a number of revisions touching many branches and having a
        // "change" in svn:mergeinfo, except it's all empty, e.g. r182326. Try to
        // parse this and silently skip it if the mergeinfo is empty.
        parse_ok = maybeParseSimpleMergeinfo(revnum, &mi);
    }
    if (parse_ok && mi.isEmpty()) {
        // all empty, ignore, this happens when we have -0,0 +0,0 changes
        // only.
        return EXIT_SUCCESS;
    }

    if (transactions.size() != 1) {
        printf(" MONKEYMERGE not sure how to handle %d transactions over %d branches!", transactions.size(), to_branches_.size());
        return EXIT_SUCCESS;
    }

    // For now, we only care when a branch is merged into head.
    if (to_branches_.size() != 1) {
        printf(" MONKEYMERGE don't know how to handle multiple branches: %s", qPrintable(to_branches_.values().join(" ")));
        return EXIT_SUCCESS;
    }

    const QString to = to_branches_.values().front();
    // NOTE: there are various vendor imports into project branches and some
    // into user branches as the focus was on importing that into head. So far,
    // I couldn't find a case where some bogus mergeinfo was copied and an
    // erroneous "vendor import" recorded.
    // As of writing, the list is this:
    // % grep -o "copy from branch.*vendor.*to.*(user|projects)/[^\"]*" log-base|sed 's/clang[0-9]*-import/clangXYZ-import/'|sort -u
    // copy from branch "refs/tags/vendor/google/googletest/1.8.1" to branch "projects/import-googletest-1.8.1
    // copy from branch "refs/tags/vendor/hyperv/20130502" to branch "projects/hyperv
    // copy from branch "refs/tags/vendor/llvm-project/master" to branch "projects/clangXYZ-import
    // copy from branch "refs/tags/vendor/llvm-project/release-10.x" to branch "projects/clangXYZ-import
    // copy from branch "refs/tags/vendor/pjdfstest/abf03c3a47745d4521b0e4aa141317553ca48f91" to branch "user/ngie/add-pjdfstest
    // copy from branch "user/keramida/vendor" to branch "user/keramida/tzcode
    // copy from branch "vendor/NetBSD/bmake/dist" to branch "projects/bmake
    // copy from branch "vendor/NetBSD/tests/dist" to branch "projects/netbsd-tests-update-12
    // copy from branch "vendor/NetBSD/tests/dist" to branch "projects/netbsd-tests-upstream-01-2017
    // copy from branch "vendor/acpica/dist" to branch "projects/acpica_20090521
    // copy from branch "vendor/binutils/dist" to branch "projects/binutils-2.17
    // copy from branch "vendor/clang/dist" to branch "projects/clang-trunk
    // copy from branch "vendor/clang/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/clang/dist" to branch "projects/clangbsd
    // copy from branch "vendor/clang/dist" to branch "projects/clangbsd-import
    // copy from branch "vendor/compiler-rt/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/compiler-rt/dist" to branch "user/ed/compiler-rt
    // copy from branch "vendor/elftoolchain/dist" to branch "projects/elftoolchain
    // copy from branch "vendor/elftoolchain/dist" to branch "projects/elftoolchain-update-r3130
    // copy from branch "vendor/flex/dist" to branch "projects/flex-sf
    // copy from branch "vendor/google/capsicum-test/dist" to branch "projects/capsicum-test
    // copy from branch "vendor/google/googletest/dist" to branch "projects/import-googletest-1.10.0
    // copy from branch "vendor/heirloom-doctools/dist" to branch "projects/doctools
    // copy from branch "vendor/hyperv/dist" to branch "projects/hyperv
    // copy from branch "vendor/illumos/dist" to branch "projects/libzfs_core
    // copy from branch "vendor/illumos/dist" to branch "user/delphij/zfs-arc-rebase
    // copy from branch "vendor/illumos/dist" to branch "user/delphij/zfs-lz4
    // copy from branch "vendor/libc++/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/libevent/dist" to branch "projects/openssl111
    // copy from branch "vendor/lld/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/lld/dist" to branch "projects/lld-import
    // copy from branch "vendor/lldb/dist" to branch "projects/clang-trunk
    // copy from branch "vendor/lldb/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/lldb/dist" to branch "projects/lldb-r201577
    // copy from branch "vendor/lldb/dist-release_90" to branch "projects/clangXYZ-import
    // copy from branch "vendor/llvm-openmp/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/llvm/dist" to branch "projects/clang-trunk
    // copy from branch "vendor/llvm/dist" to branch "projects/clangXYZ-import
    // copy from branch "vendor/llvm/dist" to branch "projects/clangbsd
    // copy from branch "vendor/llvm/dist" to branch "projects/clangbsd-import
    // copy from branch "vendor/lua/dist" to branch "projects/lua-bootloader
    // copy from branch "vendor/ncurses/dist" to branch "user/rafan/ncurses
    // copy from branch "vendor/opensolaris/dist" to branch "user/gnn/fasttrap
    // copy from branch "vendor/openssl/dist" to branch "projects/openssl-1.0.2
    // copy from branch "vendor/openssl/dist" to branch "projects/openssl111
    // copy from branch "vendor/openssl/dist" to branch "projects/openssl_098_merge_8
    // copy from branch "vendor/serf/dist" to branch "projects/openssl111
    // copy from branch "vendor/tre/dist" to branch "user/gabor/tre-integration
    // copy from branch "vendor/tzcode/dist" to branch "user/keramida/vendor
    // copy from branch "vendor/zlib/dist" to branch "user/delphij/libz

    // r229307 re-tagged 9.0 release, allow it to be a proper merge from releng
    if (to != "master" && !to.startsWith("projects/") && !to.startsWith("user/")
            && !to.startsWith("vendor/") && !to.startsWith("vendor-sys/")
            && to != "refs/tags/release/9.0.0") {
        printf(" MONKEYMERGE ignoring merge into %s", qPrintable(to));
        return EXIT_SUCCESS;
    }

    if (!parse_ok) {
        printf(" Couldn't parse mergeinfo!");
        return EXIT_SUCCESS;
    } else if (parse_ok && mi.isEmpty()) {
        // Parsed ok but maybe was a reverse-merge or something.
        return EXIT_SUCCESS;
    }

    // This is redundant with the WARN log about the branch copies. Make sure
    // output is sorted and stable for easier diff(1) comparison between runs.
    qDebug() << "Ended up with" << mi;
    for (const auto& mi : mi) {
        if (mi.from.startsWith("user") || mi.from.startsWith("user")) {
            printf(" MONKEYMERGE not merging from user, please inspect me: %s", qPrintable(mi.from));
            continue;
        }
        printf(" MONKEYMERGE IS HAPPENING!");
        Repository::Transaction *txn = transactions.value(repository + mi.to, 0);
        txn = transactions.first();
        txn->noteCopyFromBranch(mi.from, mi.rev);
    }

    return EXIT_SUCCESS;
}

int SvnRevision::fetchRevProps()
{
    if( propsFetched )
        return EXIT_SUCCESS;

    apr_hash_t *revprops;
    SVN_ERR(svn_fs_revision_proplist(&revprops, fs, revnum, pool));
    svn_string_t *svnauthor = (svn_string_t*)apr_hash_get(revprops, "svn:author", APR_HASH_KEY_STRING);
    svn_string_t *svndate = (svn_string_t*)apr_hash_get(revprops, "svn:date", APR_HASH_KEY_STRING);
    svn_string_t *svnlog = (svn_string_t*)apr_hash_get(revprops, "svn:log", APR_HASH_KEY_STRING);

    if (svnlog)
        log = svnlog->data;
    else
        log.clear();
    authorident = svnauthor ? identities.value(svnauthor->data) : QByteArray();
    epoch = svndate ? get_epoch(svndate->data) : 0;
    if (authorident.isEmpty()) {
        if (!svnauthor || svn_string_isempty(svnauthor))
            authorident = "nobody <nobody@localhost>";
        else
            authorident = svnauthor->data + QByteArray(" <") + svnauthor->data +
                QByteArray("@") + userdomain.toUtf8() + QByteArray(">");
    }
    propsFetched = true;
    return EXIT_SUCCESS;
}

int SvnRevision::commit()
{
    // now create the commit
    if (fetchRevProps() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    foreach (Repository *repo, repositories.values()) {
        repo->commit();
    }

    foreach (Repository::Transaction *txn, transactions) {
        txn->setAuthor(authorident);
        txn->setDateTime(epoch);
        txn->setLog(log);

        if (txn->commit() != EXIT_SUCCESS)
            return EXIT_FAILURE;
        delete txn;
    }

    return EXIT_SUCCESS;
}

int SvnRevision::exportEntry(const char *key, const svn_fs_path_change2_t *change,
                             apr_hash_t *changes)
{
    AprAutoPool revpool(pool.data());
    QString current = QString::fromUtf8(key);

    // was this copied from somewhere?
    svn_revnum_t rev_from = SVN_INVALID_REVNUM;
    const char *path_from = NULL;
    if (change->change_kind != svn_fs_path_change_delete) {
        // svn_fs_copied_from would fail on deleted paths, because the path
        // obviously no longer exists in the current revision
        SVN_ERR(svn_fs_copied_from(&rev_from, &path_from, fs_root, key, revpool));
    }

    // Is there mergeinfo attached? Only do this once per revnum
    // We abuse the logged_already hash for this.
    if (change->mergeinfo_mod == svn_tristate_true &&
        rev_from == SVN_INVALID_REVNUM) {
#if 0
        AprAutoPool mipool(pool.data());
        svn_mergeinfo_catalog_t catalog;
        apr_array_header_t *paths = apr_array_make(mipool, 10, sizeof(const char *));
        APR_ARRAY_PUSH(paths, const char *) = "/";

        // Seems to grab *all* mergeinfo in the repo, takes quite some time to run.
        SVN_ERR(svn_fs_get_mergeinfo(&catalog, fs_root, paths, svn_mergeinfo_explicit, true, mipool));

        for (apr_hash_index_t *i = apr_hash_first(mipool, catalog); i; i = apr_hash_next(i)) {
            const void *vkey;
            void *value;
            // XXX value is actually arrays of merge ranges
            apr_hash_this(i, &vkey, NULL, &value);
            qWarning() << "Got mergeinfo for " << (const char *)vkey;
        }
#endif
    }

    // is this a directory?
    svn_boolean_t is_dir;
    SVN_ERR(svn_fs_is_dir(&is_dir, fs_root, key, revpool));
    // Adding newly created directories
    if (is_dir && change->change_kind == svn_fs_path_change_add && path_from == NULL
        && CommandLineParser::instance()->contains("empty-dirs")) {
        QString keyQString = key;
        // Skipping SVN-directory-layout
        if (keyQString.endsWith("/trunk") || keyQString.endsWith("/branches") || keyQString.endsWith("/tags")) {
            //qDebug() << "Skipping SVN-directory-layout:" << keyQString;
            return EXIT_SUCCESS;
        }
        needCommit = true;
        //qDebug() << "Adding directory:" << key;
    }
    // svn:ignore-properties
    else if (is_dir && (change->change_kind == svn_fs_path_change_add || change->change_kind == svn_fs_path_change_modify || change->change_kind == svn_fs_path_change_replace)
             && path_from == NULL && CommandLineParser::instance()->contains("svn-ignore")) {
        needCommit = true;
    }
    else if (is_dir) {
        if (change->change_kind == svn_fs_path_change_modify ||
            change->change_kind == svn_fs_path_change_add) {
            if (path_from == NULL) {
                // freshly added directory, or modified properties
                // Git doesn't handle directories, so we don't either
                //qDebug() << "   mkdir ignored:" << key;
                return EXIT_SUCCESS;
            }

            qDebug() << "   " << key << "was copied from" << path_from << "rev" << rev_from;
        } else if (change->change_kind == svn_fs_path_change_replace) {
            if (path_from == NULL)
                qDebug() << "   " << key << "was replaced";
            else
                qDebug() << "   " << key << "was replaced from" << path_from << "rev" << rev_from;
        } else if (change->change_kind == svn_fs_path_change_reset) {
            qCritical() << "   " << key << "was reset, panic!";
            return EXIT_FAILURE;
        } else {
            // if change_kind == delete, it shouldn't come into this arm of the 'is_dir' test
            qCritical() << "   " << key << "has unhandled change kind " << change->change_kind << ", panic!";
            return EXIT_FAILURE;
        }
    } else if (change->change_kind == svn_fs_path_change_delete) {
        is_dir = wasDir(fs, revnum - 1, key, revpool);
    }

    if (is_dir)
        current += '/';

    //MultiRule: loop start
    //Replace all returns with continue,
    bool isHandled = false;
    foreach ( const MatchRuleList matchRules, allMatchRules ) {
        // find the first rule that matches this pathname
        MatchRuleList::ConstIterator match = findMatchRule(matchRules, revnum, current);
        if (match != matchRules.constEnd()) {
            const Rules::Match &rule = *match;
            if ( exportDispatch(key, change, path_from, rev_from, changes, current, rule, matchRules, revpool) == EXIT_FAILURE )
                return EXIT_FAILURE;
            isHandled = true;
        } else if (is_dir && path_from != NULL) {
            qDebug() << current << "is a copy-with-history, auto-recursing";
            if ( recurse(key, change, path_from, matchRules, rev_from, changes, revpool) == EXIT_FAILURE )
                return EXIT_FAILURE;
            isHandled = true;
        } else if (is_dir && change->change_kind == svn_fs_path_change_delete) {
            qDebug() << current << "deleted, auto-recursing";
            if ( recurse(key, change, path_from, matchRules, rev_from, changes, revpool) == EXIT_FAILURE )
                return EXIT_FAILURE;
            isHandled = true;
        }
    }
    if ( isHandled ) {
        return EXIT_SUCCESS;
    }
    if (wasDir(fs, revnum - 1, key, revpool)) {
        qDebug() << current << "was a directory; ignoring";
    } else if (change->change_kind == svn_fs_path_change_delete) {
        qDebug() << current << "is being deleted but I don't know anything about it; ignoring";
    } else {
        qCritical() << current << "did not match any rules; cannot continue";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int SvnRevision::exportDispatch(const char *key, const svn_fs_path_change2_t *change,
                                const char *path_from, svn_revnum_t rev_from,
                                apr_hash_t *changes, const QString &current,
                                const Rules::Match &rule, const MatchRuleList &matchRules, apr_pool_t *pool)
{
    switch (rule.action) {
    case Rules::Match::Ignore:
        if(ruledebug)
            qDebug() << "rev" << revnum << qPrintable(current) << "matched rule:" << rule.info() << "  " << "ignoring.";
        return EXIT_SUCCESS;

    case Rules::Match::Recurse:
        if(ruledebug)
            qDebug() << "rev" << revnum << qPrintable(current) << "matched rule:" << rule.info() << "  " << "recursing.";
        return recurse(key, change, path_from, matchRules, rev_from, changes, pool);

    case Rules::Match::Export:
        if(ruledebug)
            qDebug() << "rev" << revnum << qPrintable(current) << "matched rule:" << rule.info() << "  " << "exporting.";
        if (exportInternal(key, change, path_from, rev_from, current, rule, matchRules) == EXIT_SUCCESS)
            return EXIT_SUCCESS;
        if (change->change_kind != svn_fs_path_change_delete) {
            if(ruledebug)
                qDebug() << "rev" << revnum << qPrintable(current) << "matched rule:" << rule.info() << "  " << "Unable to export non path removal.";
            return EXIT_FAILURE;
        }
        // we know that the default action inside recurse is to recurse further or to ignore,
        // either of which is reasonably safe for deletion
        qWarning() << "WARN: deleting unknown path" << current << "; auto-recursing";
        return recurse(key, change, path_from, matchRules, rev_from, changes, pool);
    }

    // never reached
    return EXIT_FAILURE;
}

int SvnRevision::exportInternal(const char *key, const svn_fs_path_change2_t *change,
                                const char *path_from, svn_revnum_t rev_from,
                                const QString &current, const Rules::Match &rule, const MatchRuleList &matchRules)
{
    needCommit = true;
    QString svnprefix, repository, effectiveRepository, branch, path;
    splitPathName(rule, current, &svnprefix, &repository, &effectiveRepository, &branch, &path);

    to_branches_.insert(branch);

    Repository *repo = repositories.value(repository, 0);
    if (!repo) {
        if (change->change_kind != svn_fs_path_change_delete)
            qCritical() << "Rule" << rule
                        << "references unknown repository" << repository;
        return EXIT_FAILURE;
    }

    printf(".");
    fflush(stdout);
//                qDebug() << "   " << qPrintable(current) << "rev" << revnum << "->"
//                         << qPrintable(repository) << qPrintable(branch) << qPrintable(path);

    if (change->change_kind == svn_fs_path_change_delete && current == svnprefix && path.isEmpty() && !repo->hasPrefix()) {
        if(ruledebug)
            qDebug() << "repository" << repository << "branch" << branch << "deleted";
        return repo->deleteBranch(branch, revnum);
    }

    QString previous;
    QString prevsvnprefix, prevrepository, preveffectiverepository, prevbranch, prevpath;

    if (path_from != NULL) {
        previous = QString::fromUtf8(path_from);
        if (wasDir(fs, rev_from, path_from, pool.data())) {
            previous += '/';
        }
        MatchRuleList::ConstIterator prevmatch =
            findMatchRule(matchRules, rev_from, previous, NoIgnoreRule);
        if (prevmatch != matchRules.constEnd()) {
            splitPathName(*prevmatch, previous, &prevsvnprefix, &prevrepository,
                          &preveffectiverepository, &prevbranch, &prevpath);

        } else {
            qWarning() << "WARN: SVN reports a \"copy from\" @" << revnum << "from" << path_from << "@" << rev_from << "but no matching rules found! Ignoring copy, treating as a modification";
            path_from = NULL;
        }
    }

    //qDebug() << "XXX" << path_from << ":" << current << svnprefix << path << prevbranch << branch << "XXX";
    // current == svnprefix => we're dealing with the contents of the whole branch here
    if (path_from != NULL && current == svnprefix && path.isEmpty()) {
        if (previous != prevsvnprefix) {
            // source is not the whole of its branch
            qDebug() << qPrintable(current) << "is a partial branch of repository"
                     << qPrintable(prevrepository) << "branch"
                     << qPrintable(prevbranch) << "subdir"
                     << qPrintable(prevpath);
            // Sometimes we don't want to record the parent. For example in
            // revisions where a subdir from head was forked into a vendor
            // branch. This confuses `git subtree`. TODO(emaste): provide some
            // more thorough explanation.
            if (rule.branchpoint.startsWith("none")) {
                qWarning() << "Not recording" << qPrintable(current) << "as branchpoint from" << prevbranch << "rev" << rev_from;
                prevbranch.clear();
            }
        } else if (preveffectiverepository != effectiveRepository) {
            qWarning() << "WARN:" << qPrintable(current) << "rev" << revnum
                       << "is a cross-repository copy (from repository"
                       << qPrintable(prevrepository) << "branch"
                       << qPrintable(prevbranch) << "path"
                       << qPrintable(prevpath) << "rev" << rev_from << ")";
        } else if (path != prevpath) {
            // NOTE(uqs): this will happen when using the `prefix` action and a
            // vendor branch gets copied to a tag. It'll lead to a disconnected
            // tag. This shouldn't happen and needs more work.
            qWarning() << qPrintable(current)
                     << "is a branch copy which renames base directory of all contents"
                     << qPrintable(prevpath) << "to" << qPrintable(path);
            qFatal("This must not happen. Vendor tags will be disconnected.");
            // FIXME: Handle with fast-import 'file rename' facility
            //        ??? Might need special handling when path == / or prevpath == /
        } else if (!(branch.startsWith("vendor") && prevbranch == "master")) {
            // We skip e.g. r12116 which branched a vendor branch off of head
            // by deleting everything.
            if (prevbranch == branch) {
                // same branch and same repository
                qDebug() << qPrintable(current) << "rev" << revnum
                         << "is reseating branch" << qPrintable(branch)
                         << "to an earlier revision"
                         << qPrintable(previous) << "rev" << rev_from;
            } else {
                // same repository but not same branch
                // this means this is a plain branch
                qDebug() << qPrintable(repository) << ": branch"
                         << qPrintable(branch) << "is branching from"
                         << qPrintable(prevbranch);
            }

            if (!rule.branchpoint.isEmpty()) {
                const auto pair = rule.branchpoint.splitRef('@');
                qWarning() << "Not recording" << qPrintable(current) << "as branchpoint from" << prevbranch << "rev" << rev_from;
                prevbranch.clear();
                if (pair.size() != 2) {
                    qFatal("Please provide none@<treehash> or otherbranch@ref for this sort of branch creation!");
                } else {
                    Repository::Transaction *txn = transactions.value(repository + branch, 0);
                    if (!txn) {
                        txn = repo->newTransaction(branch, svnprefix, revnum);
                        if (!txn)
                            return EXIT_FAILURE;

                        transactions.insert(repository + branch, txn);
                    }
                    if (pair[0] == "none") {
                        qDebug() << "Creating branch from" << __FILE__ << __LINE__;
                        if (repo->createBranch(branch, revnum, pair[1].toString(), txn) == EXIT_FAILURE)
                            return EXIT_FAILURE;
                    } else {
                        qDebug() << "Creating branch from" << __FILE__ << __LINE__;
                        if (repo->createBranch(branch, revnum, pair[0].toString(), pair[1].toString().toInt()) == EXIT_FAILURE)
                            return EXIT_FAILURE;
                    }
                }
            } else {
                qDebug() << "Creating branch from" << __FILE__ << __LINE__;
                if (repo->createBranch(branch, revnum, prevbranch, rev_from) == EXIT_FAILURE)
                    return EXIT_FAILURE;
            }

            if(CommandLineParser::instance()->contains("svn-branches")) {
                Repository::Transaction *txn = transactions.value(repository + branch, 0);
                if (!txn) {
                    txn = repo->newTransaction(branch, svnprefix, revnum);
                    if (!txn)
                        return EXIT_FAILURE;

                    transactions.insert(repository + branch, txn);
                }
                if(ruledebug)
                    qDebug() << "Create a true SVN copy of branch (" << key << "->" << branch << path << ")";
                txn->deleteFile(path);
                recursiveDumpDir(txn, fs, fs_root, key, path, pool, revnum, rule, matchRules, ruledebug);
            }
            if (rule.annotate) {
                // create an annotated tag
                fetchRevProps();
                repo->createAnnotatedTag(branch, svnprefix, revnum, authorident,
                                         epoch, log);
            }
            return EXIT_SUCCESS;
        }
    }
    Repository::Transaction *txn = transactions.value(repository + branch, 0);
    if (!txn) {
        txn = repo->newTransaction(branch, svnprefix, revnum);
        if (!txn)
            return EXIT_FAILURE;

        transactions.insert(repository + branch, txn);
    }

    //
    // If this path was copied from elsewhere, use it to infer _some_
    // merge points.  This heuristic is fairly useful for tracking
    // changes across directory re-organizations and wholesale branch
    // imports.
    //
    // NOTE(uqs): HACK ALERT! Only merge between head, projects, and user
    // branches for the FreeBSD repositories. Never merge into stable or
    // releng, as we only ever cherry-pick changes to those branches.
    // Also, never merge from stable, like was done in SVN r306097, as it pulls
    // in all history. Never merge from user as well, as it full of MFCs and
    // pointless back and forth merges, e.g. r248449
    if (path_from != NULL && prevrepository == repository && prevbranch != branch
            && (branch.startsWith("master") || branch.startsWith("projects") || branch.startsWith("user") || branch.startsWith("vendor") || branch.startsWith("refs/tags/vendor"))
            // If branching into vendor, the source must not be master,
            // otherwise *all* the history will be pulled into the vendor
            // branch.
            // NOTE(uqs): we allow vendor → vendor "merges" or rather
            // branchpoints for the 2-3 cases where a vendor was just renamed.
            // There's also r185205 which is basically a pure copy of a vendor
            // area, so that one is fine.
            && !((branch.startsWith("vendor") || branch.startsWith("refs/tags/vendor")) && prevbranch == "master")
            // Don't merge the mess that is various user branches into head.
            // They'll just end up as cherry picks instead.
            && !(branch.startsWith("master") && prevbranch.startsWith("user"))
            // this stops IFCs from being recorded, as there isn't much value in them.
            // So master -> project is a cherrypick, not a merge.
            //&& !(branch.startsWith("projects") && prevbranch.startsWith("master"))
            && !prevbranch.isEmpty()
            && !prevbranch.startsWith("stable")) {
        QStringList log = QStringList()
                          << "copy from branch" << prevbranch << "to branch"
                          << branch << "@rev" << QString::number(rev_from);
        if (!logged_already_.contains(qHash(log))) {
            logged_already_.insert(qHash(log));
            qDebug() << "copy from branch" << prevbranch << "to branch"
                     << branch << "@rev" << rev_from;
        }
        if (rule.branchpoint.startsWith("none")) {
            qWarning() << "Not recording" << qPrintable(current) << "as branchpoint from" << prevbranch << "rev" << rev_from;
        } else {
            merge_from_rev_ = rev_from;
            merge_from_branch_ = prevbranch;
            txn->noteCopyFromBranch (prevbranch, rev_from);
        }
    }

    if (change->change_kind == svn_fs_path_change_replace && path_from == NULL) {
        if(ruledebug)
            qDebug() << "replaced with empty path (" << branch << path << ")";
        txn->deleteFile(path);
    }
    if (change->change_kind == svn_fs_path_change_delete) {
        if(ruledebug)
            qDebug() << "delete (" << branch << path << ")";
        txn->deleteFile(path);
    } else if (!current.endsWith('/')) {
        if(ruledebug)
            qDebug() << "add/change file (" << key << "->" << branch << path << ")";
        dumpBlob(txn, fs_root, key, path, pool);
    } else {
        if(ruledebug)
            qDebug() << "add/change dir (" << key << "->" << branch << path << ")";

        // Check unknown svn-properties
        if (((path_from == NULL && change->prop_mod==1) || (path_from != NULL && (change->change_kind == svn_fs_path_change_add || change->change_kind == svn_fs_path_change_replace)))
            && CommandLineParser::instance()->contains("propcheck")) {
            if (fetchUnknownProps(pool, key, fs_root) != EXIT_SUCCESS) {
                qWarning() << "Error checking svn-properties (" << key << ")";
            }
        }

        // When we're frobbing things, don't "deleteall" in the fast-import stream. This happens with an emtpy path.
        if (path == NULL && rule.branchpoint.startsWith("none@")) {
            const auto pair = rule.branchpoint.splitRef('@');
            qDebug() << "Creating branch from" << __FILE__ << __LINE__;
            if (repo->createBranch(branch, revnum, pair[1].toString(), txn) == EXIT_FAILURE)
                return EXIT_FAILURE;
        } else if (path == NULL && rule.branchpoint == "none") {
        } else {
            txn->deleteFile(path);
        }

        // Add GitIgnore with svn:ignore
        int ignoreSet = false;
        if (((path_from == NULL && change->prop_mod==1) || (path_from != NULL && (change->change_kind == svn_fs_path_change_add || change->change_kind == svn_fs_path_change_replace)))
            && CommandLineParser::instance()->contains("svn-ignore")) {
            QString svnignore;
            // TODO: Check if svn:ignore or other property was changed, but always set on copy/rename (path_from != NULL)
            if (fetchIgnoreProps(&svnignore, pool, key, fs_root) != EXIT_SUCCESS) {
                qWarning() << "Error fetching svn-properties (" << key << ")";
            } else if (!svnignore.isNull()) {
                addGitIgnore(pool, key, path, fs_root, txn, svnignore.toStdString().c_str());
                ignoreSet = true;
            }
        }

        // Add GitIgnore for empty directories (if GitIgnore was not set previously)
        if (CommandLineParser::instance()->contains("empty-dirs") && ignoreSet == false) {
            if (addGitIgnore(pool, key, path, fs_root, txn) == EXIT_SUCCESS) {
                return EXIT_SUCCESS;
            }
        }

        recursiveDumpDir(txn, fs, fs_root, key, path, pool, revnum, rule, matchRules, ruledebug);
    }

    if (rule.annotate) {
        // create an annotated tag
        fetchRevProps();
        repo->createAnnotatedTag(branch, svnprefix, revnum, authorident,
                                 epoch, log);
    }

    if (!rule.branchpoint.isEmpty() && rule.branchpoint != "none") {
        const auto pair = rule.branchpoint.splitRef('@');
        if (pair.size() != 2) {
            qFatal("Please provide branch@<revnum> for this sort of merge record!");
        } else {
            txn->noteCopyFromBranch(pair[0].toString(), pair[1].toInt());
        }
    }

    // These are a once per-rev actions, but we end up here for every path that
    // has matched. That is, we emit tons and tons of redundant deletes into
    // the fast-import stream. We can't drain the list either, as the rule
    // match is marked const. Gather them all up for handling in
    // SvnRevision::prepareTransactions
    if (!rule.deletes.empty()) {
        const QString key = repository + branch;
        if (!deletions_.contains(key)) {
            deletions_[key] = QSet<QString>();
        }
        for (auto const& path : rule.deletes) {
            deletions_[key].insert(path);
        }
    }
    if (!rule.renames.empty()) {
        const QString key = repository + branch;
        if (!renames_.contains(key)) {
            renames_[key] = QMap<QString, QString>();
        }
        for (auto const& from_to : rule.renames) {
            renames_[key].insert(from_to.first, from_to.second);
        }
    }

    return EXIT_SUCCESS;
}

int SvnRevision::recurse(const char *path, const svn_fs_path_change2_t *change,
                         const char *path_from, const MatchRuleList &matchRules, svn_revnum_t rev_from,
                         apr_hash_t *changes, apr_pool_t *pool)
{
    svn_fs_root_t *fs_root = this->fs_root;
    if (change->change_kind == svn_fs_path_change_delete)
        SVN_ERR(svn_fs_revision_root(&fs_root, fs, revnum - 1, pool));

    // get the dir listing
    svn_node_kind_t kind;
    SVN_ERR(svn_fs_check_path(&kind, fs_root, path, pool));
    if(kind == svn_node_none) {
        qWarning() << "WARN: Trying to recurse using a nonexistant path" << path << ", ignoring";
        return EXIT_SUCCESS;
    } else if(kind != svn_node_dir) {
        qWarning() << "WARN: Trying to recurse using a non-directory path" << path << ", ignoring";
        return EXIT_SUCCESS;
    }

    apr_hash_t *entries;
    SVN_ERR(svn_fs_dir_entries(&entries, fs_root, path, pool));
    AprAutoPool dirpool(pool);

    // While we get a hash, put it in a map for sorted lookup, so we can
    // repeat the conversions and get the same git commit hashes.
    QMap<QByteArray, svn_node_kind_t> map;
    for (apr_hash_index_t *i = apr_hash_first(pool, entries); i; i = apr_hash_next(i)) {
        dirpool.clear();
        const void *vkey;
        void *value;
        apr_hash_this(i, &vkey, NULL, &value);
        svn_fs_dirent_t *dirent = reinterpret_cast<svn_fs_dirent_t *>(value);
        map.insertMulti(QByteArray(dirent->name), dirent->kind);
    }

    QMapIterator<QByteArray, svn_node_kind_t> i(map);
    while (i.hasNext()) {
        dirpool.clear();
        i.next();
        QByteArray entry = path + QByteArray("/") + i.key();
        QByteArray entryFrom;
        if (path_from)
            entryFrom = path_from + QByteArray("/") + i.key();

        // check if this entry is in the changelist for this revision already
        svn_fs_path_change2_t *otherchange =
            (svn_fs_path_change2_t*)apr_hash_get(changes, entry.constData(), APR_HASH_KEY_STRING);
        if (otherchange && otherchange->change_kind == svn_fs_path_change_add) {
            qDebug() << entry << "rev" << revnum
                     << "is in the change-list, deferring to that one";
            continue;
        }

        QString current = QString::fromUtf8(entry);
        if (i.value() == svn_node_dir)
            current += '/';

        // find the first rule that matches this pathname
        MatchRuleList::ConstIterator match = findMatchRule(matchRules, revnum, current);
        if (match != matchRules.constEnd()) {
            if (exportDispatch(entry, change, entryFrom.isNull() ? 0 : entryFrom.constData(),
                               rev_from, changes, current, *match, matchRules, dirpool) == EXIT_FAILURE)
                return EXIT_FAILURE;
        } else {
            if (i.value() == svn_node_dir) {
                qDebug() << current << "rev" << revnum
                         << "did not match any rules; auto-recursing";
                if (recurse(entry, change, entryFrom.isNull() ? 0 : entryFrom.constData(),
                            matchRules, rev_from, changes, dirpool) == EXIT_FAILURE)
                    return EXIT_FAILURE;
            }
        }
    }

    return EXIT_SUCCESS;
}

int SvnRevision::addGitIgnore(apr_pool_t *pool, const char *key, QString path,
                              svn_fs_root_t *fs_root, Repository::Transaction *txn, const char *content)
{
    // Check for number of subfiles if no content
    if (!content) {
        apr_hash_t *entries;
        SVN_ERR(svn_fs_dir_entries(&entries, fs_root, key, pool));
        // Return if any subfiles
        if (apr_hash_count(entries)!=0) {
            return EXIT_FAILURE;
        }
    }

    // Add gitignore-File
    QString gitIgnorePath = path + ".gitignore";
    if (content) {
        QIODevice *io = txn->addFile(gitIgnorePath, 33188, strlen(content));
        if (!CommandLineParser::instance()->contains("dry-run")) {
            io->write(content);
            io->putChar('\n');
        }
    } else {
        QIODevice *io = txn->addFile(gitIgnorePath, 33188, 0);
        if (!CommandLineParser::instance()->contains("dry-run")) {
            io->putChar('\n');
        }
    }

    return EXIT_SUCCESS;
}

int SvnRevision::fetchIgnoreProps(QString *ignore, apr_pool_t *pool, const char *key, svn_fs_root_t *fs_root)
{
    // Get svn:ignore
    svn_string_t *prop = NULL;
    SVN_ERR(svn_fs_node_prop(&prop, fs_root, key, "svn:ignore", pool));
    if (prop) {
        *ignore = QString(prop->data);
        // remove patterns with slashes or backslashes,
        // they didn't match anything in Subversion but would in Git eventually
        ignore->remove(QRegExp("^[^\\r\\n]*[\\\\/][^\\r\\n]*(?:[\\r\\n]|$)|[\\r\\n][^\\r\\n]*[\\\\/][^\\r\\n]*(?=[\\r\\n]|$)"));
        // add a slash in front to have the same meaning in Git of only working on the direct children
        ignore->replace(QRegExp("(^|[\\r\\n])\\s*(?![\\r\\n]|$)"), "\\1/");
    } else {
        *ignore = QString();
    }

    // Get svn:global-ignores
    prop = NULL;
    SVN_ERR(svn_fs_node_prop(&prop, fs_root, key, "svn:global-ignores", pool));
    if (prop) {
        QString global_ignore = QString(prop->data);
        // remove patterns with slashes or backslashes,
        // they didn't match anything in Subversion but would in Git eventually
        global_ignore.remove(QRegExp("^[^\\r\\n]*[\\\\/][^\\r\\n]*(?:[\\r\\n]|$)|[\\r\\n][^\\r\\n]*[\\\\/][^\\r\\n]*(?=[\\r\\n]|$)"));
        ignore->append(global_ignore);
    }

    // replace multiple asterisks Subversion meaning by Git meaning
    ignore->replace(QRegExp("\\*+"), "*");

    return EXIT_SUCCESS;
}

int SvnRevision::fetchUnknownProps(apr_pool_t *pool, const char *key, svn_fs_root_t *fs_root)
{
    // Check all properties
    apr_hash_t *table;
    SVN_ERR(svn_fs_node_proplist(&table, fs_root, key, pool));
    apr_hash_index_t *hi;
    void *propVal;
    const void *propKey;
    for (hi = apr_hash_first(pool, table); hi; hi = apr_hash_next(hi)) {
        apr_hash_this(hi, &propKey, NULL, &propVal);
        if (strcmp((char*)propKey, "svn:ignore")!=0 && strcmp((char*)propKey, "svn:global-ignores")!=0 && strcmp((char*)propKey, "svn:mergeinfo") !=0) {
            qWarning() << "WARN: Unknown svn-property" << (char*)propKey << "set to" << ((svn_string_t*)propVal)->data << "for" << key;
        }
    }

    return EXIT_SUCCESS;
}
