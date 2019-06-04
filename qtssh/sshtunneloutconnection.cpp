#include "sshtunneloutconnection.h"
#include "sshtunnelout.h"
#include "sshclient.h"

Q_LOGGING_CATEGORY(logsshtunneloutconnection, "ssh.tunnelout.connection")
Q_LOGGING_CATEGORY(logsshtunneloutconnectiontransfer, "ssh.tunnelout.connection.transfer")

SshTunnelOutConnection::SshTunnelOutConnection(const QString &name, SshClient *client, QTcpSocket *sock, quint16 remotePort, QObject *parent)
    : QObject(parent)
    , m_parent(qobject_cast<SshTunnelOut*>(parent))
    , m_state(Creating)
    , m_client(client)
    , m_sock(sock)
    , m_port(remotePort)
    , m_name(QString(name + ":%1").arg(sock->localPort()))
    , m_tx_start_ptr(m_tx_buffer)
    , m_rx_start_ptr(m_rx_buffer)
    , m_tx_stop_ptr(m_tx_buffer)
    , m_rx_stop_ptr(m_rx_buffer)
{
    QObject::connect(m_sock, &QTcpSocket::readyRead,
                     this,   &SshTunnelOutConnection::_socketDataReceived);

    QObject::connect(m_sock, &QTcpSocket::disconnected,
                     this,   &SshTunnelOutConnection::_socketDisconnected);

    QObject::connect(m_sock, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
                     this,   &SshTunnelOutConnection::_socketError);

    QObject::connect(client, &SshClient::sshDataReceived,
                     this,   &SshTunnelOutConnection::_sshDataReceived);

    _creating();
}

void SshTunnelOutConnection::disconnectFromHost()
{
    if(m_sock->state() == QTcpSocket::ConnectedState)
    {
        qCDebug(logsshtunneloutconnection) << m_name << "Ask disconnectFromHost";
        m_sock->disconnectFromHost();
    }
}

bool SshTunnelOutConnection::isClosed()
{
    return (m_sock == nullptr);
}

int SshTunnelOutConnection::_displaySshError(const QString &msg)
{
    char *emsg;
    int size;
    int ret = libssh2_session_last_error(m_client->session(), &emsg, &size, 0);
    if(ret == LIBSSH2_ERROR_EAGAIN)
    {
        /* Process next connection */
        _sshWaiting = true;
        return LIBSSH2_ERROR_EAGAIN;
    }
    qCCritical(logsshtunneloutconnection) << m_name << "Error" << ret << msg << QString(emsg);
    disconnectFromHost();
    m_state = ConnectionState::Error;
    return ret;
}

ssize_t SshTunnelOutConnection::_transferSshToRx()
{
    ssize_t len = 0;
    if(m_channel != nullptr)
    {
        do
        {
            len = static_cast<ssize_t>(libssh2_channel_read(m_channel, m_rx_buffer, BUFFER_SIZE));
            if (len < 0)
            {
                return _displaySshError(QString("libssh2_channel_read (%1 / %2)").arg(len).arg(BUFFER_SIZE));
            }
            if(len > 0)
            {
                m_rx_start_ptr = m_rx_buffer;
                m_rx_stop_ptr  = m_rx_buffer + len;
                qCDebug(logsshtunneloutconnectiontransfer) << m_name << " libssh2_channel_read return " << len << "bytes";
                _transferRxToSock();
            }
        }
        while(len > 0);
    }

    return len;
}

ssize_t SshTunnelOutConnection::_transferRxToSock()
{
    ssize_t len = 0;

    if(m_sock == nullptr || m_sock->state() != QTcpSocket::ConnectedState)
        return -1;

    while (m_rx_start_ptr < m_rx_stop_ptr)
    {
        len = m_sock->write(m_rx_start_ptr, m_rx_stop_ptr - m_rx_start_ptr);
        if (len <= 0)
        {
            qCWarning(logsshtunneloutconnectiontransfer) << "ERROR : " << m_name << " local failed to write (" << len << ")";
            return SOCKET_WRITE_ERROR;
        }
        qCDebug(logsshtunneloutconnectiontransfer) << m_name << len << "bytes written on socket";
        m_rx_start_ptr += len;
    }
    m_rx_start_ptr = m_rx_buffer;
    m_rx_stop_ptr = m_rx_buffer;
    return len;
}

ssize_t SshTunnelOutConnection::_transferSockToTx()
{
    qint64 len = 0;
    if(m_sock == nullptr && m_sock->state() != QAbstractSocket::ConnectedState)
    {
        qCCritical(logsshtunneloutconnectiontransfer) << m_name << "_transferSockToTx on invalid socket";
        return -1;
    }

    if(m_tx_stop_ptr < (m_tx_buffer + BUFFER_SIZE))
    {
        len = m_sock->read(m_tx_stop_ptr, BUFFER_SIZE - (m_tx_stop_ptr - m_tx_buffer));
        if(len > 0)
        {
             qCDebug(logsshtunneloutconnectiontransfer) << m_name << " read on socket return " << len << "bytes";
             m_tx_stop_ptr += len;
        }
        else if(len < 0)
        {
            qCWarning(logsshtunneloutconnectiontransfer) << m_name << " read on socket return " << len << m_sock->errorString();
        }
    }
    else
    {
        qCDebug(logsshtunneloutconnectiontransfer) << m_name << " TX buffer full";
    }
    if((m_tx_stop_ptr - m_tx_start_ptr) > 0 && (m_state == ConnectionState::Running))
    {
        _transferTxToSsh();
    }

    return len;
}

ssize_t SshTunnelOutConnection::_transferTxToSsh()
{
    ssize_t len = 0;

    if(m_channel != nullptr)
    {
        while((m_tx_stop_ptr - m_tx_start_ptr) > 0)
        {
            len = libssh2_channel_write(m_channel, m_tx_start_ptr, static_cast<size_t>(m_tx_stop_ptr - m_tx_start_ptr));
            if (len < 0)
            {
                return _displaySshError("libssh2_channel_write");
            }
            if (len == 0)
            {
                qCWarning(logsshtunneloutconnectiontransfer) << "ERROR : " << m_name << " libssh2_channel_write return 0";
                return 0;
            }
            qCDebug(logsshtunneloutconnectiontransfer) << m_name << " write on SSH return " << len << "bytes";
            m_tx_start_ptr += len;
        }
        m_tx_start_ptr = m_tx_buffer;
        m_tx_stop_ptr = m_tx_buffer;

        if(m_sock->bytesAvailable())
        {
            _transferSockToTx();
        }
    }
    return len;
}

int SshTunnelOutConnection::_creating()
{
    if ( ! m_client->takeChannelCreationMutex(this) )
    {
        return LIBSSH2_ERROR_EAGAIN;
    }
    m_channel = libssh2_channel_direct_tcpip(m_client->session(),  "127.0.0.1", m_port);
    m_client->releaseChannelCreationMutex(this);
    if(m_channel == nullptr)
    {
        return _displaySshError("libssh2_channel_direct_tcpip");
    }
    qCDebug(logsshtunneloutconnection)  << m_name << "libssh2_channel_direct_tcpip OK";
    m_state = ConnectionState::Running;
    _running();
    return 0;
}

int SshTunnelOutConnection::_running()
{
    if((m_tx_stop_ptr - m_tx_start_ptr) > 0)
    {
        /* Something to send on SSH */
        _transferTxToSsh();
    }
    _transferSshToRx();

    if(m_channel != nullptr)
    {
        if (libssh2_channel_eof(m_channel))
        {
            _disconnectedFromSsh = true;
            qCDebug(logsshtunneloutconnection) << "Set Disconnected from ssh";
        }
    }

    if(_disconnectedFromSsh && m_sock && (m_rx_stop_ptr - m_rx_start_ptr == 0) &&  (m_tx_stop_ptr - m_tx_start_ptr == 0))
    {
        qCDebug(logsshtunneloutconnection) << "Disconnect socket for disconnected from Ssh";
        m_sock->disconnectFromHost();
    }

    return 0;
}


int SshTunnelOutConnection::_freeing()
{
    if(m_channel != nullptr)
    {
        int ret = libssh2_channel_free(m_channel);
        if(ret)
        {
            return _displaySshError("libssh2_channel_free");
        }
        m_channel = nullptr;
    }
    qCDebug(logsshtunneloutconnection)  << m_name << "libssh2_channel_free OK";
    m_state = ConnectionState::None;
    m_sock->disconnect();
    QObject::connect(m_sock, &QObject::destroyed, this, &SshTunnelOutConnection::_socketDestroyed);
    delete m_sock;
    m_sock = nullptr;
    return 0;
}

void SshTunnelOutConnection::_socketDataReceived()
{
    _transferSockToTx();
}

void SshTunnelOutConnection::_socketDisconnected()
{
    /* Identify client socket */
    qCDebug(logsshtunneloutconnection) << m_name << "_socketDisconnected()";
    if(m_sock->state() != QTcpSocket::UnconnectedState)
    {
        qCWarning(logsshtunneloutconnection) << m_name << "_socketDisconnected but is" << m_sock->state();
    }
    m_state = ConnectionState::Freeing;
    _freeing();
}

void SshTunnelOutConnection::_socketDestroyed()
{
    qCDebug(logsshtunneloutconnection)  << m_name << "_socketDestroyed OK";
    m_sock = nullptr;
    m_parent->_removeClosedConnection(this);
}

void SshTunnelOutConnection::_socketError()
{
    auto error = m_sock->error();
    switch(error)
    {
        case QAbstractSocket::RemoteHostClosedError:
            qCDebug(logsshtunneloutconnection) << m_name << "socket RemoteHostClosedError";
            // Socket will be closed just after this, nothing to care about
            break;
        default:
            qCWarning(logsshtunneloutconnection) << m_name << "socket error=" << error << m_sock->errorString();
    }
}

void SshTunnelOutConnection::_sshDataReceived()
{
    _sshWaiting = false;
    int ret;
    switch(m_state)
    {
    case ConnectionState::Creating:
        ret =_creating();
        break;
    case ConnectionState::Running:
        ret =_running();
        break;
    case ConnectionState::Freeing:
        ret = _freeing();
        break;
    case ConnectionState::None:
        ret = 0;
        break;
    case ConnectionState::Error:
        ret = -1;
        break;
    }
    if(ret != 0 && ret != LIBSSH2_ERROR_EAGAIN)
    {
        qCWarning(logsshtunneloutconnection) << m_name << "State machine error: " << m_state << ret;
    }
}

void SshTunnelOutConnection::_sshDisconnected()
{
    m_channel = nullptr;
    m_sock->disconnectFromHost();
}