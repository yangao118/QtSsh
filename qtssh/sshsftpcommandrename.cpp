#include "sshsftpcommandrename.h"
#include "sshclient.h"

SshSftpCommandRename::SshSftpCommandRename(const QString &origin, const QString &newname, SshSFtp &parent)
    : SshSftpCommand(parent)
    , m_origin(origin)
    , m_newname(newname)
{
    setName(QString("rename(%1->%2)").arg(origin).arg(newname));
}

bool SshSftpCommandRename::error() const
{
    return m_error;
}


void SshSftpCommandRename::process()
{
    int res;
    switch(m_state)
    {
    case Openning:
        res = libssh2_sftp_rename_ex(
                    sftp().getSftpSession(),
                    qPrintable(m_origin),
                    static_cast<unsigned int>(m_origin.size()),
                    qPrintable(m_newname),
                    static_cast<unsigned int>(m_newname.size()),
                    LIBSSH2_SFTP_RENAME_OVERWRITE | \
                    LIBSSH2_SFTP_RENAME_ATOMIC | \
                    LIBSSH2_SFTP_RENAME_NATIVE
                    );

        if(res < 0)
        {
            if(res == LIBSSH2_ERROR_EAGAIN)
            {
                return;
            }
            m_error = true;
            qCWarning(logsshsftp) << "SFTP rename error " << res;
        }
        else
        {
            setState(CommandState::Terminate);
            FALLTHROUGH;
        }
    case Terminate:
        break;

    case Error:
        break;

    default:
        setState(CommandState::Terminate);
        break;
    }
}
