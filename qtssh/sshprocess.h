#pragma once

#include "sshchannel.h"
#include <QSemaphore>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logsshprocess)

class SshProcess : public SshChannel
{
    Q_OBJECT

protected:
    explicit SshProcess(const QString &name, SshClient *client);
    friend class SshClient;

public:
    virtual ~SshProcess() override;
    void close() override;
    QByteArray result();
    bool isError();

public slots:
    void runCommand(const QString &cmd);
    void sshDataReceived() override;

    void runCommandSync(const QString &cmd, int timeout_val = 3000 /* ms */);



private:
    QString m_cmd;
    LIBSSH2_CHANNEL *m_sshChannel {nullptr};
    QByteArray m_result;
    bool m_error {false};

signals:
    void finished();
    void failed();
};
