//
// Created by dingjing on 1/11/25.
//

#ifndef backup_qt_BACKUP_H
#define backup_qt_BACKUP_H
#include <QObject>


class BackupPrivate;
class Backup final : public QObject
{
    Q_OBJECT
public:
    explicit Backup(const QString &filePath, QObject *parent = nullptr);
    ~Backup() override;

    bool backup();
    bool restore();
    static QStringList getAllBackupFiles();

    void test();

private:
    BackupPrivate*          d_ptr = nullptr;
    Q_DECLARE_PRIVATE(Backup);
};


#endif // backup_qt_BACKUP_H
