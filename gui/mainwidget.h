#ifndef LOGINSIGNOUTWGT_H
#define LOGINSIGNOUTWGT_H

#include <QWidget>

#include "gui/deviceinfowgt.h"

class QNetworkAccessManager;

namespace Ui {
class MainWgt;
}

class MainWgt : public QWidget
{
    Q_OBJECT
    enum class SignUpSteps {undef_step,     //only to first call goToNextStep
                            login_signup,
                            deviceInfo, customerInfo,
                            installing, finished,
                            num_steps = finished};
public:
    explicit MainWgt (QWidget *parent = nullptr);

    ~MainWgt ();

private:
    Ui::MainWgt *ui;

    SignUpSteps curStep {SignUpSteps::undef_step};

    void goToCurStep ();

    void changeDeviceDetected (bool isDetected);

    QNetworkAccessManager* m_manager {nullptr};

private slots:
    void goToNextStep ();

    void onLoginSignUp (QString name);

    void onLogout ();

    void onBack ();

    void onStart ();

    void onStartNew ();

    void onSuccessInstall ();

    void onFailInstall ();

    void onErrorHandling (QString title, QString what, QString where, QString details);

    void onConnected (bool isConnected);

    void onCustomerComplete (bool isComplete);

    void onSerialNumberChanged ();

};

#endif // LOGINSIGNOUTWGT_H
