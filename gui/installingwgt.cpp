#include "installingwgt.h"
#include "ui_installingwgt.h"

#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QApplication>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QThreadPool>
#include <QDir>
#include <QTimer>

#include "rsaencryption.h"
#include "aesencryption.h"
#include "apkinstallworker.h"
#include "utils.h"
#include "credentionals.h"
#include "installfilesgenerator.h"

InstallingWgt::InstallingWgt(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::InstallingWgt)
{
    ui->setupUi(this);

    m_manager = new QNetworkAccessManager(this);

    timerPB = new QTimer (this);
    timerPB->setInterval(100);

    connect (timerPB, &QTimer::timeout,
             this, &InstallingWgt::onTimeoutPB);
}

InstallingWgt::~InstallingWgt()
{
    if (tmp_dir)
        delete tmp_dir;
    delete ui;
}

void InstallingWgt::onSerialNumberChanged ()
{
    if (nullptr != m_worker) {
        m_worker->cancel ();
    }
}

void InstallingWgt::onTimeoutPB ()
{
    auto curValue = ui->progressBarInstall->property("dbValue").toDouble ();
    curValue += (100.f - curValue) / 20;
    ui->progressBarInstall->setValue(curValue);
    ui->progressBarInstall->setProperty("dbValue", curValue);
}

void InstallingWgt::onInstallError (QString title, QString what, QString where, QString details)
{
    isInstallError = true;
    m_output += details;
    emit sigError (title, what, where, m_output);
}

void InstallingWgt::startInstalling ()
{
    isInstallError = false;
    m_output.clear ();
    ui->progressBarInstall->setValue(0);
    ui->progressBarInstall->setProperty("dbValue", 0.f);
    timerPB->start ();
    sendApkRequest ();
}

void InstallingWgt::setIdDevice (QString idDevice)
{
    idDevice_ = idDevice;
}

void InstallingWgt::generateRsa()
{
    m_rsaEncryption.generate();
    m_output.append(m_rsaEncryption.publicKey());

}

void InstallingWgt::sendApkRequest()
{
    //generate RSA
    writeLog("Generate RSA...");
    m_rsaEncryption.generate();
    writeLog("[OK] Generate RSA");

    QString endpoint = defWebServerEndpoint;
    const QUrl url(endpoint+"/encrypt");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QByteArray data = m_rsaEncryption.publicKey().toUtf8();
    writeLog(QString("Pub key:%1").arg(QString(data)));

    writeLog("Send request 'encrypt'...");
    QNetworkReply* reply = m_manager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if(reply->error() == QNetworkReply::NoError){

            writeLog("[OK] Send request 'encrypt'");
            QString resp = QString(reply->readAll());

            QJsonDocument parser(QJsonDocument::fromJson(resp.toUtf8()));
            QJsonObject json = parser.object();

            QByteArray key;
            QString id;

            if(json.contains("Key")){
                auto encodedKey = json.value("Key").toString().toLocal8Bit();
                key = m_rsaEncryption.decryptPriBase64(encodedKey);
            }

            if(json.contains("Id")){
                id = json.value("Id").toString();
            }

            bool isValid = !key.isEmpty() && !id.isEmpty();
            if(isValid){
                runDownloadFile(id,key);
            }else{
               writeLog(QString("[FAILED] Parse response.'%1'").arg(resp));
               onInstallError ("Error: WebApp",
                               "Webapp error: error occured during parsing response from WebApp",
                               "An error occured during parsing response from WebApp. See \"Details\"\n"
                               "section to get more detailed information about the error.",
                               m_output);
               emit sigFail ();
            }

        }
        else {
            writeLog(QString("[FAILED] Send request 'encrypt'. Err:'%1'").arg(reply->errorString()));
            onInstallError ("Error: WebApp",
                            "Webapp error: error occured during sending requests to WebApp",
                            "An error occured during sending requests to WebApp. See \"Details\"\n"
                            "section to get more detailed information about the error.",
                            m_output);
            emit sigFail ();
        }
        reply->deleteLater();
    });

}

void InstallingWgt::runDownloadFile(const QString &id, const QByteArray &key)
{
    QString endpoint = defWebServerEndpoint;
    const QUrl url(endpoint+"/download?id="+id);
    QNetworkRequest request(url);
    writeLog(QString("Donload file..."));
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [=](){
        if(reply->error() == QNetworkReply::NoError){
            writeLog(QString("[OK] Download file"));
            installApkOnDevice (reply->readAll(), key);
            /*QString appFolder = qApp->applicationDirPath();
            QString encodedFilePath = QString("%1/%2").arg(appFolder, id);
            writeLog("Save file...");
            bool ok = saveToDisk(encodedFilePath, reply);
            if(ok){
                writeLog(QString("[OK] Save file to:'%1'").arg(encodedFilePath));
                installApkOnDevice ();
                //try decrypt
                AesEncryption aes;

                tmp_dir = new QTemporaryDir ();

                if (!tmp_dir->isValid ()) {
                    writeLog ( QString ("[FAILED] Error while creating tmp file"));
                    emit sigFail ();
                    onInstallError ("Error: Decryption",
                                    "Decryption error: error occured while file decription",
                                    "An error occured while file decription. See \"Details\"\n"
                                    "section to get more detailed information about the error.",
                                    m_output);
                }
                else {
                    tmp_file = tmp_dir->path () + "/apkserver.apk";

                    int ret = aes.dectyptFile(encodedFilePath, key, tmp_file);
                    if(ret == 0){
                        writeLog( QString("[OK] Decrypt file"));
                        installApkOnDevice ();
                    }else{
                        writeLog( QString("[FAILED] Decrypt file"));
                        emit sigFail ();
                        onInstallError ("Error: Decryption",
                                        "Decryption error: error occured while file decription",
                                        "An error occured while file decription. See \"Details\"\n"
                                        "section to get more detailed information about the error.",
                                        m_output);
                        delete tmp_dir;
                        tmp_dir = nullptr;
                        tmp_file.clear ();
                    }
                }
                //remove encoded file
                QFile::remove(encodedFilePath);

            }else{
                writeLog(QString("[FAILED] Save file to:'%1'").arg(encodedFilePath));
                emit sigFail ();
                onInstallError ("Error: Save File",
                                "Save File error: error occured while file was saving in disk",
                                "An error occured while file was saving in disk. See \"Details\"\n"
                                "section to get more detailed information about the error.",
                                m_output);
            }*/

        }else{
            writeLog(QString("[FAILED] Download file. Err:'%1'").arg(reply->errorString()));
            emit sigFail ();
        }
        reply->deleteLater();
    });
}

bool InstallingWgt::saveToDisk(const QString &filename, QIODevice *data)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        QString dbg =QString("Could not open %1 for writing: %2")
                .arg(filename, file.errorString());

        writeLog(dbg);
        return false;
    }

    file.write(data->readAll());
    file.close();

    return true;
}

void InstallingWgt::writeLog(const QString &msg)
{
    m_output.append(msg);
}


void InstallingWgt::installApkOnDevice (const QByteArray &apkData, const QByteArray &key)
{
    QString apkFilaPath = qApp->applicationDirPath() +"/app-release.apk";
    apkFilaPath = tmp_file;
    QString packageName = "com.example.testrsaencryption/.MainActivity";
    QString deviceFoder = defDeviceFoder;
    QString pubFileNameApk1 = "keyApk1.pub";
    QString pubFileName = "key.pub";

    QString tmpFolder = qApp->applicationDirPath()+"/tmp";
    if(!QFile::exists(tmpFolder)){
        QDir dir;
        dir.mkpath(tmpFolder);
    }

    QString localFolder = tmpFolder;

    if(m_worker == nullptr){
        m_worker = new ApkInstallWorker(apkData, key, packageName, deviceFoder, pubFileName, pubFileNameApk1, localFolder, idDevice_.toUtf8());
        connect(m_worker, &ApkInstallWorker::message, this, &InstallingWgt::writeLog, Qt::QueuedConnection );
        connect(m_worker, &ApkInstallWorker::sigError, this, &InstallingWgt::onInstallError, Qt::QueuedConnection );
        connect(m_worker, &ApkInstallWorker::finished, this, &InstallingWgt::onCompleteWorker, Qt::QueuedConnection);
        connect(m_worker, &ApkInstallWorker::started, this,&InstallingWgt::onStartWorker, Qt::QueuedConnection );
        m_pool.start(m_worker);
    }
}

void InstallingWgt::onStartWorker()
{
}

void InstallingWgt::onCompleteWorker()
{
    if (tmp_dir)
        delete tmp_dir;
    tmp_file.clear ();
    tmp_dir = nullptr;
    m_worker = nullptr;
    timerPB->stop ();
    ui->progressBarInstall->setValue(100);
    if (isInstallError || m_output.contains ("[FAILED]"))
        emit sigFail ();
    else
        emit sigSuccess ();
}

void InstallingWgt::onStopInstall()
{
    if(m_worker == nullptr){
        return;
    }

    m_worker->cancel();
}

