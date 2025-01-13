//
// Created by dingjing on 1/11/25.
//

#include "backup.h"

#include <QDir>
#include <QFile>
#include <QList>
#include <QDebug>
#include <QDateTime>

#include <gio/gio.h>
#include <sys/stat.h>


#define BACKUP_VERSION              1
#define BACKUP_STR                  "andsec-backup"
#define BREAK_IF_FAIL(x)            if (!(x)) { break; }
#define BREAK_NULL(x)               if ((x) == nullptr) { break; }
#define RETURN_IF_FAIL(x)           if (!(x)) { return; }
#define RETURN_VAL_IF_FAIL(x, val)  if (!(x)) { return (val); }
#define NOT_NULL_RUN(x,f,...)       do { if (x) { f(x, ##__VA_ARGS__); x = nullptr; } } while(0)


class BackupPrivate
{
public:
    explicit BackupPrivate(Backup* q);
    void setFilePath(const QString& path);
    static QStringList getAllMountPoints();
    static bool doCopy(const QString& src, const QString& dst);
    static void updatePermission(const QString& src, const QString& dst);
    QString getExtName() const;
    QString getFileName() const;
    QString getMountPoint() const;
    QString getFilePathMD5() const;
    QString getRestorePath() const;
    QString getFileContentMD5() const;
    bool makeBackupDirsIfNeeded();
    bool parseMeta();
    bool writeMeta();
    bool doBackup();
    bool doRestore();

private:
    bool                    mIsUpload;
    QString                 mMountPoint;
    QString                 mContextMD5;

    // -- start
    int                     mVersion = 1;
    QString                 mFilePath;
    QString                 mFilePathMD5;

    QString                 mFileCtxMD51;
    QString                 mFileCtxMD52;
    QString                 mFileCtxMD53;

    quint64                 mFileTimestamp1;
    quint64                 mFileTimestamp2;
    quint64                 mFileTimestamp3;
    // -- end

    Backup*                 q_ptr;
    Q_DECLARE_PUBLIC(Backup);
};

static const char* gsFileExt[] = {
    ".tar.gz",
    ".tar.xz",
    ".docx",
    ".xlsx",
    ".java",
    ".pptx",
    ".bz2",
    ".cpp",
    ".dxf",
    ".doc",
    ".hpp",
    ".odp",
    ".odt",
    ".ods",
    ".pdf",
    ".ppt",
    ".rar",
    ".txt",
    ".tmp",
    ".wps",
    ".wps",
    ".xls",
    ".zip",
    ".bz",
    ".cc",
    ".py",
    ".xz",
    ".c",
    ".h",
    nullptr,
};

BackupPrivate::BackupPrivate(Backup* q)
    : q_ptr(q)
{
}

void BackupPrivate::setFilePath(const QString& path)
{
    int i = 0;
    const int fLen = strlen(path.toUtf8().constData());
    auto pathTmp = static_cast<char*>(malloc(fLen + 1));
    memset(pathTmp, 0, fLen + 1);
    memcpy(pathTmp, path.toUtf8().constData(), fLen);
    for (i = 0; '\0' != pathTmp[i]; ++i) {
        while (pathTmp[i] && '/' == pathTmp[i] && '/' == pathTmp[i + 1]) {
            for (int j = i; pathTmp[j] || j < fLen; pathTmp[j] = pathTmp[j + 1], ++j);
        }
    }
    if (i - 1 >= 0 && pathTmp[i - 1] == '/') {
        pathTmp[i - 1] = '\0';
    }
    mFilePath = pathTmp;

    NOT_NULL_RUN(pathTmp, free);
}

QString BackupPrivate::getMountPoint() const
{
    if (mFilePath.isEmpty() || !mFilePath.startsWith("/")) {
        return {};
    }

    QStringList mp = getAllMountPoints();
    for (auto m : mp) {
        if (mFilePath.startsWith(m)) {
            return m;
        }
    }

    return {};
}

QStringList BackupPrivate::getAllMountPoints()
{
    QByteArray      bufLine;
    QList<QString>  mountPoints;
    const char* mpFile = nullptr;

#define MTAB "/etc/mtab"

    if (QFile::exists(MTAB)) {
        mpFile = MTAB;
    }

    RETURN_VAL_IF_FAIL(mpFile, mountPoints);

    QFile file(mpFile);
    RETURN_VAL_IF_FAIL(file.open(QIODevice::ReadOnly), mountPoints);

    while (!(bufLine = file.readLine()).isEmpty()) {
        if ('/' != bufLine[0]) {
            continue;
        }
        for (int i = 0; bufLine[i] != '\0'; i++) {
            if (bufLine[i] == '\t') {
                bufLine[i] = ' ';
            }
        }
        auto strArr = QString(bufLine).split(" ");
        if (strArr.length() > 2 && strArr.at(1).length() > 0 && strArr.at(0).at(0) == '/') {
            mountPoints << strArr.at(1);
        }
    }

    file.close();

    auto compare = [=] (const QString& str1, const QString& str2) -> int {
        return str1.length() > str2.length();
    };

    std::sort(mountPoints.begin(), mountPoints.end(), compare);

    return mountPoints;
}

bool BackupPrivate::doCopy(const QString & src, const QString & dst)
{
    RETURN_VAL_IF_FAIL(QFile::exists(src), false);

    QFile::remove(dst);

    GFile* f1 = g_file_new_for_path(src.toUtf8().constData());
    GFile* f2 = g_file_new_for_path(dst.toUtf8().constData());

    g_file_copy(f1, f2, (GFileCopyFlags)(G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA), nullptr, nullptr, nullptr, nullptr);

    return QFile::exists(dst);
}

void BackupPrivate::updatePermission(const QString & src, const QString & dst)
{
    RETURN_IF_FAIL(QFile::exists(src));

    struct stat st;
    if (0 == stat(src.toUtf8().constData(), &st)) {
        chown(dst.toUtf8().constData(), st.st_uid, st.st_gid);
        chmod(dst.toUtf8().constData(), st.st_mode);
    }
}

QString BackupPrivate::getFileName() const
{
    RETURN_VAL_IF_FAIL(!mFilePath.isEmpty(), "");

    QString fileName = mFilePath.split("/").last();

    return fileName;
}

QString BackupPrivate::getExtName() const
{
    RETURN_VAL_IF_FAIL(!mFilePath.isEmpty(), "");

    const QString name = getFileName().toLower();
    for (int i = 0; gsFileExt[i] != nullptr; i++) {
        if (name.endsWith(gsFileExt[i])) {
            return gsFileExt[i];
        }
    }

    const auto extStrArr = name.split(".");
    if (extStrArr.length() > 1) {
        return QString(".%1").arg(extStrArr.last());
    }

    return {};
}

QString BackupPrivate::getFilePathMD5() const
{
    char* res = nullptr;
    GChecksum* cs = nullptr;

    do {
        cs = g_checksum_new(G_CHECKSUM_MD5);
        BREAK_NULL(cs);
        g_checksum_update(cs, reinterpret_cast<const guchar*>(mFilePath.toUtf8().constData()), static_cast<gssize>(strlen(mFilePath.toUtf8().constData())));
        res = g_strdup(g_checksum_get_string(cs));

    } while (false);

    NOT_NULL_RUN(cs, g_checksum_free);

    return res;
}

QString BackupPrivate::getRestorePath() const
{
    RETURN_VAL_IF_FAIL(!mFilePath.isEmpty(), "");

    auto getTimeStr = [=] (quint64 timestamp) -> QString {
        QDateTime tim = QDateTime::fromMSecsSinceEpoch(timestamp * 1000);
        return tim.toString("yyyyMMddhhmmss");
    };

    const QString extName = getExtName();
    QString restorePath = mFilePath;
    restorePath.chop(extName.length());

    if (!mFileCtxMD53.isEmpty()) {
        restorePath.append("-");
        restorePath.append(getTimeStr(mFileTimestamp3));
        restorePath.append(extName);
        return restorePath;
    }

    if (!mFileCtxMD52.isEmpty()) {
        restorePath.append("-");
        restorePath.append(getTimeStr(mFileTimestamp2));
        restorePath.append(extName);
        return restorePath;
    }

    if (!mFileCtxMD51.isEmpty()) {
        restorePath.append("-");
        restorePath.append(getTimeStr(mFileTimestamp1));
        restorePath.append(extName);
        return restorePath;
    }

    return {};
}

QString BackupPrivate::getFileContentMD5() const
{
    QString res;
    size_t bufLen = 0;
    FILE* fr = nullptr;
    guchar buf[1024] = {0};
    GChecksum* cs = nullptr;

    do {
        fr = fopen(mFilePath.toUtf8().constData(), "r");
        BREAK_NULL(fr);

        cs = g_checksum_new(G_CHECKSUM_MD5);
        BREAK_NULL(cs);

        while ((bufLen = fread (buf, 1, sizeof(buf), fr)) > 0) {
            g_checksum_update(cs, buf, static_cast<gssize>(bufLen));
        }
        res = (g_checksum_get_string(cs));

    } while (false);

    NOT_NULL_RUN(fr, fclose);
    NOT_NULL_RUN(cs, g_checksum_free);

    return res;
}

bool BackupPrivate::makeBackupDirsIfNeeded()
{
    RETURN_VAL_IF_FAIL(!mMountPoint.isEmpty(), false);

    const auto metaDir = QString("%1/.%2/meta").arg(mMountPoint, BACKUP_STR);
    const auto backupDir = QString("%1/.%2/backup").arg(mMountPoint, BACKUP_STR);
    RETURN_VAL_IF_FAIL(g_mkdir_with_parents(metaDir.toUtf8().constData(), 0755) >= 0, false);
    RETURN_VAL_IF_FAIL(g_mkdir_with_parents(backupDir.toUtf8().constData(), 0755) >= 0, false);

    return true;
}

bool BackupPrivate::parseMeta()
{
    mIsUpload = false;
    mVersion = BACKUP_VERSION;
    mFileTimestamp1 = 0;
    mFileTimestamp2 = 0;
    mFileTimestamp3 = 0;
    mFileCtxMD51 = "";
    mFileCtxMD52 = "";
    mFileCtxMD53 = "";

    const auto metaPath = QString("%1/.%2/meta/%3").arg(mMountPoint, BACKUP_STR, mFilePathMD5);
    QFile metaFile(metaPath);
    RETURN_VAL_IF_FAIL(metaFile.open(QIODevice::ReadOnly), false);

    const QString ctx = metaFile.readAll();
    RETURN_VAL_IF_FAIL(!ctx.isEmpty(), false);

    const auto strArr = ctx.split("{]");
    RETURN_VAL_IF_FAIL(!strArr.empty(), false);

    const int ver = strArr.at(0).toInt();
    if (1 == ver) {
        RETURN_VAL_IF_FAIL(strArr.length() == 10, false);

        mIsUpload = (strArr.at(3).toInt() == 1);

        mFileCtxMD51 = strArr.at(4);
        mFileTimestamp1 = strArr.at(5).toLongLong();

        mFileCtxMD52 = strArr.at(6);
        mFileTimestamp2 = strArr.at(7).toLongLong();

        mFileCtxMD53 = strArr.at(8);
        mFileTimestamp3 = strArr.at(9).toLongLong();
    }

    return true;
}

bool BackupPrivate::writeMeta()
{
    QFile metaFile(QString("%1/.%2/meta/%3").arg(mMountPoint, BACKUP_STR, mFilePathMD5));
    RETURN_VAL_IF_FAIL(metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text), false);

    const auto metaFileCtx = QString("%1{]%2{]%3{]%4{]%5{]%6{]%7{]%8{]%9{]%10")
            .arg(mVersion).arg(mFilePath).arg(mFilePathMD5).arg(mIsUpload ? "1" : "0")
            .arg(mFileCtxMD51).arg(mFileTimestamp1)
            .arg(mFileCtxMD52).arg(mFileTimestamp2)
            .arg(mFileCtxMD53).arg(mFileTimestamp3);

    metaFile.write(metaFileCtx.toUtf8());
    metaFile.flush();
    metaFile.close();

    return true;
}

bool BackupPrivate::doBackup()
{
    const auto b1 = QString("%1/.%2/backup/%3-1").arg(mMountPoint, BACKUP_STR, mFilePathMD5);
    const auto b2 = QString("%1/.%2/backup/%3-2").arg(mMountPoint, BACKUP_STR, mFilePathMD5);
    const auto b3 = QString("%1/.%2/backup/%3-3").arg(mMountPoint, BACKUP_STR, mFilePathMD5);

    const auto copyFileAndWriteMeta = [=] (const QString& dstFilePath) ->bool {
        QFile::remove(dstFilePath);
        bool ret = doCopy(mFilePath, dstFilePath);
        if (ret) {
            ret = writeMeta();
        }

        return ret;
    };

    if (!mFileCtxMD53.isEmpty()) {
        RETURN_VAL_IF_FAIL(mFileCtxMD53 != mContextMD5, true);
        mFileTimestamp1 = mFileTimestamp2;
        mFileTimestamp2 = mFileTimestamp3;
        mFileTimestamp3 = QDateTime::currentSecsSinceEpoch();

        QFile::remove(b1);

        mFileCtxMD51 = mFileCtxMD52;
        QFile::rename(b2, b1);

        mFileCtxMD52 = mFileCtxMD53;
        QFile::rename(b3, b2);

        mFileCtxMD53 = mContextMD5;
        return copyFileAndWriteMeta(b3);
    }

    if (!mFileCtxMD52.isEmpty()) {
        RETURN_VAL_IF_FAIL(mFileCtxMD52 != mContextMD5, true);
        mFileTimestamp3 = QDateTime::currentSecsSinceEpoch();
        mFileCtxMD53 = mContextMD5;
        return copyFileAndWriteMeta(b3);
    }

    if (!mFileCtxMD51.isEmpty()) {
        RETURN_VAL_IF_FAIL(mFileCtxMD51 != mContextMD5, true);
        mFileTimestamp2 = QDateTime::currentSecsSinceEpoch();
        mFileCtxMD52= mContextMD5;
        return copyFileAndWriteMeta(b2);
    }

    mFileTimestamp1 = QDateTime::currentSecsSinceEpoch();
    mFileCtxMD51= mContextMD5;

    return copyFileAndWriteMeta(b1);
}

bool BackupPrivate::doRestore()
{
    RETURN_VAL_IF_FAIL(!mFilePath.isEmpty(), false);

    const QString restPath = getRestorePath();

    if (!mFileCtxMD53.isEmpty()) {
        const auto s1 = QString("%1/.%2/backup/%3-3").arg(mMountPoint, BACKUP_STR, mFilePathMD5);
        return doCopy(s1, restPath);
    }

    if (!mFileCtxMD52.isEmpty()) {
        const auto s2 = QString("%1/.%2/backup/%3-2").arg(mMountPoint, BACKUP_STR, mFilePathMD5);
        return doCopy(s2, restPath);
    }

    if (!mFileCtxMD51.isEmpty()) {
        const auto s2 = QString("%1/.%2/backup/%3-1").arg(mMountPoint, BACKUP_STR, mFilePathMD5);
        return doCopy(s2, restPath);
    }

    return false;
}

Backup::Backup(const QString & filePath, QObject * parent)
    : QObject(parent), d_ptr(new BackupPrivate(this))
{
    Q_D(Backup);

    d->setFilePath(filePath);
}

Backup::~Backup()
{
    delete d_ptr;
}

bool Backup::backup()
{
    Q_D(Backup);

    d->mMountPoint = d->getMountPoint();
    RETURN_VAL_IF_FAIL(!d->mMountPoint.isEmpty(), false);

    RETURN_VAL_IF_FAIL(d->makeBackupDirsIfNeeded(), false);

    d->mFilePathMD5 = d->getFilePathMD5();
    RETURN_VAL_IF_FAIL(!d->mFilePathMD5.isEmpty(), false);

    d->mContextMD5 = d->getFileContentMD5();
    RETURN_VAL_IF_FAIL(!d->mContextMD5.isEmpty(), false);
    d->parseMeta();
    RETURN_VAL_IF_FAIL(d->doBackup(), false);

    return true;
}

bool Backup::restore()
{
    Q_D(Backup);

    d->mMountPoint = d->getMountPoint();
    RETURN_VAL_IF_FAIL(!d->mMountPoint.isEmpty(), false);

    d->mFilePathMD5 = d->getFilePathMD5();
    RETURN_VAL_IF_FAIL(!d->mFilePathMD5.isEmpty(), false);

    RETURN_VAL_IF_FAIL(d->parseMeta(), false);

    RETURN_VAL_IF_FAIL(d->doRestore(), false);

    return true;
}

void Backup::test()
{
    Q_D(Backup);

    qInfo() << d->getAllMountPoints();
    qInfo() << "[Backup]: " << backup();
    qInfo() << "[Restore]: " << restore();
}
