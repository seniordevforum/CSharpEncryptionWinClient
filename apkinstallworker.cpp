#include "apkinstallworker.h"

#include <QThread>
#include <QMutexLocker>
#include <QFile>
#include <QRandomGenerator>

#include "adbwrapper.h"
#include "aesencryption.h"
#include "installfilesgenerator.h"
#include "gui/utils.h"

ApkInstallWorker::ApkInstallWorker(QByteArray apkFileData
                                     , QByteArray keyDecrypted
                                     , QString packageName
                                     , QString deviceFolder
                                     , QString publicKeyFileName
                                     , QString publicKeyFileNameApk1
                                     , QString localFolder
                                     , QString id)
    : QObject(), QRunnable()
    , m_deviceFolder(deviceFolder)
    , m_publicKeyFileName(publicKeyFileName)
    , m_publicKeyFileNameApk1 (publicKeyFileNameApk1)
    , m_packageName(packageName)
    , m_apkFileData(apkFileData)
    , m_keyDecrypted(keyDecrypted)
    , m_localFolder(localFolder)
    , m_id (id)
    , m_canceled(false)
{

}

void ApkInstallWorker::run()
{
    emit started();
    QString res;
    bool isError {false};
    QProcess::ProcessError error {QProcess::UnknownError};

    AdbWrapper().clearFolderOnDevice(m_deviceFolder,res, isError, error);

    m_state = InstallStates::ClearDeviceFolderState;

    //m_state = InstallStates::WaitPublicKeyState;
    while (m_state != InstallStates::CompleteState)
    {
        QThread::msleep(10);

        if(isCanceled()){
            m_state = InstallStates::CompleteState;
            break;
        }

        switch (m_state) {
        case ClearDeviceFolderState:
        {
            emit message("Clear device folder...");
            AdbWrapper adb;
            QString ret;
            adb.clearFolderOnDevice(m_deviceFolder, ret, isError, error);
            emit message(ret);
            if (isError) {
                m_state = InstallStates::CompleteState;
                m_lastError = "Failed to clear device folder";
                emit sigError( "Error: Adb Module",
                            AdbWrapper::errorWhat(error),
                            AdbWrapper::errorWhere(),
                            AdbWrapper::errorDetails(error) + "\n" + m_lastError);
            }
            else
                m_state = InstallStates::StartApk1;
        }
            break;
        case StartApk1:
        {
            bool ok = doRunApk (defApk1Package);
            QString msg = ok?"[OK] Run APK":"[FAILED] Run APK";

            if(ok){
                m_state = static_cast <InstallStates> (m_state + 1);
            }else{
                m_state = InstallStates::CompleteState;
                m_lastError = "Failed to run apk";
            }

            emit message(msg);
        }
            break;
        case WaitPublicKeyStateApk1:
        {
            doWaitPublicKey (m_publicKeyFileNameApk1);
        }
            break;
        case ReceivePublicKeyStateApk1:
        {
            doReceivePublicKey (m_publicKeyFileNameApk1);
        }
            break;
        case ReEncryptApk2:
        {
            reEncryptApk ();
        }
            break;
        case LoadPublicKeyStateApk1:
        {
            doLoadPublicKey (m_publicKeyFileNameApk1);
        }
            break;
        case PushApkState:
        {
            bool ok = doPushApk();

            QString msg = ok?"[OK] Install APK":"[FAILED] Install APK";

            if(ok){
                m_state = InstallStates::WaitPublicKeyState;
            }else{
                m_state = InstallStates::CompleteState;
                m_lastError = "Failed to push apk";
            }

            emit message(msg);
        }
            break;
//        case RunApkState:
//        {
//            bool ok = doRunApk();
//            QString msg = ok?"[OK] Run APK":"[FAILED] Run APK";

//            if(ok){
//                m_state = InstallStates::WaitPublicKeyState;
//            }else{
//                m_state = InstallStates::CompleteState;
//                m_lastError = "Failed to run apk";
//            }

//            emit message(msg);
//            emit message("Wait public key...");
//        }
//            break;
        case WaitPublicKeyState:
        {
            doWaitPublicKey (m_publicKeyFileName);
        }
            break;
        case ReceivePublicKeyState:
        {
            doReceivePublicKey (m_publicKeyFileName);
        }
            break;
        case LoadPublicKeyState:
        {
            doLoadPublicKey (m_publicKeyFileName);
        }
            break;
        case GenerateInstallFilesState:
        {
            doGenerateInstallFiles();
        }
            break;
        case PushInstallFilesState:
        {
            doPushInstallFiles();
        }
            break;
        case CompleteState:
        {
            if (!m_lastError.isEmpty()) {
                emit sigError( "Error: ApkInstaller module",
                            "WindowsInstaller.ApkInstaller: " + m_lastError,
                            "An error occured during apk installation. See \"Details\"\n"
                            "section to get more detailed information about the error.",
                            "\n" + m_lastError);
            }
            m_lastError.clear ();
        }
            break;
        default:
            break;
        }
    }

    for(const auto & path:m_filesToClean){
        QFile::remove(path);
    }

    emit message("complete");
    emit finished();
}

void ApkInstallWorker::cancel()
{
    QMutexLocker _locker(&m_cancelMutex);
    m_canceled = true;
}

bool ApkInstallWorker::isCanceled()
{
    QMutexLocker _locker(&m_cancelMutex);
    return m_canceled;
}

bool ApkInstallWorker::doPushApk()
{
    emit message(QString("Install apk... '%1'").arg(m_apk2FilePath));
    AdbWrapper adb;
    QString ret;
    bool isError {false};
    QProcess::ProcessError error {QProcess::UnknownError};

    bool ok = adb.copyFileToDevice(m_keyApk2FilePath,m_deviceFolder, isError, error);
    QString msg = QString(ok ?"[OK]":"[FAILED]");
    msg += "Copy file " + m_keyApk2FilePath;

    emit message(msg);

    if (ok) {
        ok = adb.installApk(m_apk2FilePath, ret, isError, error);
        emit message(ret);
    }

    if (isError) {
        ok = false;
        emit sigError( "Error: Adb Module",
                    AdbWrapper::errorWhat(error),
                    AdbWrapper::errorWhere(),
                    AdbWrapper::errorDetails(error));
    }
    return ok;
}

bool ApkInstallWorker::doRunApk(QString packageName)
{
    emit message(QString("Run apk..."));
    AdbWrapper adb;
    QString ret;
    bool isError {false};
    QProcess::ProcessError error {QProcess::UnknownError};
    bool ok = adb.runApk(packageName, ret, isError, error);
    emit message(ret);
    if (isError) {
        ok = false;
        emit sigError( "Error: Adb Module",
                    AdbWrapper::errorWhat(error),
                    AdbWrapper::errorWhere(),
                    AdbWrapper::errorDetails(error));
    }
    return ok;
}

void ApkInstallWorker::doWaitPublicKey(const QString &pubKey)
{
    AdbWrapper adb;
    QString resp;
    bool isError {false};
    QProcess::ProcessError error {QProcess::UnknownError};
    bool fileExists = adb.checkFileOnDevice(m_deviceFolder, pubKey, resp, isError, error);
    if(fileExists){
        m_state = static_cast <InstallStates> (m_state + 1);
    }
    if (isError) {
        m_state = InstallStates::CompleteState;
        emit sigError( "Error: Adb Module",
                    AdbWrapper::errorWhat(error),
                    AdbWrapper::errorWhere(),
                    AdbWrapper::errorDetails(error));
    }
}

void ApkInstallWorker::reEncryptApk ()
{
    AesEncryption aes;
    QBuffer buffSource;
    buffSource.setBuffer (&m_apkFileData);
    QBuffer buffDecrypted;
    int ret = aes.dectyptBuffer (&buffSource, &buffDecrypted, m_keyDecrypted);
    bool ok = false;
    if(ret == 0){
        InstallFilesGenerator generator(m_localFolder);
        m_installFileList.clear ();
        ok = generator.generateApk2(m_apkRsaPublicKeyData, m_apkFileData, m_apk2FilePath, m_keyApk2FilePath);
        if (ok)
            m_state = InstallStates::PushApkState;
    }

    if (!ok){
        m_state = InstallStates::CompleteState;
        emit sigError(  "Error: Decryption",
                        "Decryption error: error occured while file decription",
                        "An error occured while file decription. See \"Details\"\n"
                        "section to get more detailed information about the error.",
                        "");
    }
}

void ApkInstallWorker::doReceivePublicKey (const QString & pubKey)
{
    emit message("Copy public key file from device...");
    AdbWrapper adb;
    const QString fullPath = QString("%1/%2").arg(m_deviceFolder, pubKey);
    bool isError {false};
    QProcess::ProcessError error {QProcess::UnknownError};
    bool ok = adb.copyFileFromDevice(fullPath,m_localFolder, isError, error);
    if(ok){
        emit message("[OK] Copy public key file from device");
        m_state = static_cast <InstallStates> (m_state + 1);
    }else{
        m_lastError = "[FAILED] Copy public key file from device";
        emit message(m_lastError);
        m_state = InstallStates::CompleteState;
    }
    if (isError) {
        m_state = InstallStates::CompleteState;
        emit sigError( "Error: Adb Module",
                    AdbWrapper::errorWhat(error),
                    AdbWrapper::errorWhere(),
                    AdbWrapper::errorDetails(error));
    }
}

void ApkInstallWorker::doLoadPublicKey (const QString & pubKey)
{

    emit message("Load RSA public key from file...");
    QString fullPath = QString("%1/%2").arg(m_localFolder, pubKey);

    QFile file(fullPath);
    if(!file.open(QIODevice::ReadOnly)){
        m_lastError = file.errorString();
        emit message(QString("[FAILED] Load public key from file. Err:").arg(m_lastError));
        m_state = InstallStates::CompleteState;
    }
    else
    {
        m_apkRsaPublicKeyData = file.readAll();
        file.close();

        if(m_apkRsaPublicKeyData.isEmpty())
        {
            m_lastError = "Empty RSA public key file";
            emit message("[FAILED] Load RSA public key from file. Empty file.");
            m_state = InstallStates::CompleteState;
        }
        else
        {
            emit message("[OK] Load RSA public key from file");
            m_state = static_cast <InstallStates> (m_state + 1);
        }

    }

    m_filesToClean << fullPath;
}

void ApkInstallWorker::doGenerateInstallFiles()
{
    emit message("Generate install files...");

    m_installFileList.clear();
    InstallFilesGenerator generator(m_localFolder);
    bool ok = generator.generate(m_apkRsaPublicKeyData, m_id, m_installFileList);
    if(ok) {
        emit message("[OK] Generate install files");
        m_state = InstallStates::PushInstallFilesState;
    }
    else{
        emit message("[FAILED] Generate install files");
        m_state = InstallStates::CompleteState;
        m_lastError = "[FAILED] Generate install files";
    }
}

void ApkInstallWorker::doPushInstallFiles()
{
    AdbWrapper adb;
    emit message("Copy install files to device...");
    if(!m_installFileList.isEmpty()){
        for(auto file : m_installFileList){
            bool isError {false};
            QProcess::ProcessError error {QProcess::UnknownError};
            bool ok = adb.copyFileToDevice(file,m_deviceFolder, isError, error);
            QString msg = QString(ok ?"[OK]":"[FAILED]");
            msg += "Copy file " + file;

            emit message(msg);

            if (isError) {
                m_state = InstallStates::CompleteState;
                emit sigError( "Error: Adb Module",
                            AdbWrapper::errorWhat(error),
                            AdbWrapper::errorWhere(),
                            AdbWrapper::errorDetails(error));
            }
        }

        m_filesToClean.append(m_installFileList);
        m_installFileList.clear();
    }

    m_state = InstallStates::CompleteState;
}

void ApkInstallWorker::clearLocalFolder()
{

}

