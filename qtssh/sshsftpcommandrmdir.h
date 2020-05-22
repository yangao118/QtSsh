#ifndef SSHSFTPCOMMANDRMDIR_H
#define SSHSFTPCOMMANDRMDIR_H

#include <QObject>
#include <sshsftpcommand.h>

class SshSftpCommandRmDir : public SshSftpCommand
{
    Q_OBJECT
    const QString &m_path;
    bool m_error {false};

public:
    SshSftpCommandRmDir(const QString &path, SshSFtp &parent);
    void process() override;
    bool error() const;
};

#endif // SSHSFTPCOMMANDRMDIR_H
