#ifndef SSHSFTPCOMMANDRENAME_H
#define SSHSFTPCOMMANDRENAME_H

#include <QObject>
#include <sshsftpcommand.h>

class SshSftpCommandRename : public SshSftpCommand
{
    Q_OBJECT
    const QString &m_origin;
    const QString &m_newname;
    bool m_error {false};

public:
    SshSftpCommandRename(const QString &origin, const QString &newname, SshSFtp &parent);
    void process() override;
    bool error() const;
};

#endif // SSHSFTPCOMMANDRENAME_H
