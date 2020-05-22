#include "sshsftpcommandrmdir.h"
#include "sshclient.h"

SshSftpCommandRmDir::SshSftpCommandRmDir(const QString &path, SshSFtp &parent)
    : SshSftpCommand(parent)
    , m_path(path)
{
    setName(QString("rmdir(%1)").arg(path));
}

bool SshSftpCommandRmDir::error() const
{
    return m_error;
}

void SshSftpCommandRmDir::process()
{
    int res;
    switch(m_state)
    {
    case Openning:
        res = libssh2_sftp_rmdir_ex(
                    sftp().getSftpSession(),
                    qPrintable(m_path),
                    static_cast<unsigned int>(m_path.size())
                    );

        if(res < 0)
        {
            if(res == LIBSSH2_ERROR_EAGAIN)
            {
                return;
            }
            m_error = true;
            qCWarning(logsshsftp) << "SFTP rmdir error " << res;
            setState(CommandState::Error);
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
