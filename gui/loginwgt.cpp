#include "loginwgt.h"
#include "ui_loginwgt.h"

#include <QShowEvent>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMessageAuthenticationCode>
#include <QJsonDocument>
#include <QJsonObject>

#include "settingkeys.h"
#include "utils.h"
#include "credentionals.h"

LoginWgt::LoginWgt(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LoginWgt)
{
    ui->setupUi(this);

    connect (ui->labelForgotPassword, &QLabel::linkActivated,
             this, &LoginWgt::sigForgotPasswordActivated);
    connect (ui->labelSignUp, &QLabel::linkActivated,
             this, &LoginWgt::sigSignUp);
    connect (ui->pushButtonLogin, &QPushButton::clicked,
             this, &LoginWgt::onLogin);
    connect (ui->lineEditUsername, &QLineEdit::textChanged,
             this, &LoginWgt::checkConditionsLogin);
    connect (ui->lineEditPassword, &QLineEdit::textChanged,
             this, &LoginWgt::checkConditionsLogin);
    connect (ui->pushButtonQuestion, &QPushButton::clicked,
             this, &LoginWgt::onQuestion);


    ui->lineEditPassword->setEchoMode (QLineEdit::Password);

    //event filter
    ui->lineEditUsername->installEventFilter (this);
    ui->lineEditPassword->installEventFilter (this);

    m_manager = new QNetworkAccessManager(this);

}

bool LoginWgt::eventFilter (QObject *watched, QEvent *event)
{
    if (QEvent::KeyRelease == event->type ()
        && (ui->lineEditUsername == watched || ui->lineEditPassword == watched)) {

        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (Qt::Key_Return == keyEvent->key ()
            && ui->pushButtonLogin->isEnabled ()) {
            onLogin ();
            return true;
        }
    }
    return QWidget::eventFilter (watched, event);
}

void LoginWgt::onQuestion ()
{
    auto mode = ui->lineEditPassword->echoMode ();
    bool onView = mode == QLineEdit::Normal;
    ui->lineEditPassword->setEchoMode (onView ? QLineEdit::Password : QLineEdit::Normal);
    changeProperty (ui->pushButtonQuestion, "visiblePassw", !onView);
}

void LoginWgt::checkConditionsLogin ()
{
    ui->pushButtonLogin->setEnabled (!ui->lineEditUsername->text ().isEmpty ()
                                     && !ui->lineEditPassword->text ().isEmpty ());
}

void LoginWgt::onLogin ()
{
#if 1
    auto newEmail = ui->lineEditUsername->text ();
    auto newPassword = ui->lineEditPassword->text ();
    auto newHashPassw = QCryptographicHash::hash (newPassword.toUtf8(),
                                                  QCryptographicHash::Sha256).toHex ();
    Credentionals::instance().setData (newEmail, newHashPassw);
    const QUrl url (defWebAppEndpoint);
    QNetworkRequest request (url);
    request.setHeader (QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader ("Type", "3");
    request.setRawHeader ("Auth", Credentionals::instance ().authHeader ());

    QNetworkReply* reply = m_manager->post (request, "");

    connect(reply, &QNetworkReply::finished, this, [=](){
        if(reply->error() == QNetworkReply::NoError){
            auto resp = reply->readAll ();
            auto doc = QJsonDocument::fromJson (resp);
            auto obj = doc.object ();
            QString fN = obj ["FName"].toString ();
            QString lN = obj ["LName"].toString ();
            emit sigSuccess (fN + " " + lN);

        }else{
            auto resp = reply->readAll ();
            emit sigError       ( "Error: WebApp",
                                "Webapp error: error occured during checking invitation in WebApp",
                                "An error occured during sending requests to WebApp. See \"Details\"\n"
                                "section to get more detailed information about the error.",
                                reply->errorString() + resp);
        }
        reply->deleteLater();
    });

#else
    auto email = ui->lineEditUsername->text ();
    auto password = ui->lineEditPassword->text ();
    auto hashPassw = QString::fromStdString (QCryptographicHash::hash(password.toUtf8(),
                              QCryptographicHash::Sha256).toHex().toStdString ());

    //send to WebApp
    {
        QJsonObject obj;
        obj["UserEmail"]    = email;
        obj["Password"]     = hashPassw;
        QJsonDocument doc(obj);

        QString endpoint = defEndpoint;
        const QUrl url(endpoint+"/login");
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply* reply = m_manager->post (request, doc.toJson ());

        connect(reply, &QNetworkReply::finished, this, [=](){
            if(reply->error() != QNetworkReply::NoError){
                emit sigError   ("Error: WebApp",
                                "Webapp error: error occured during sending post to WebApp",
                                "An error occured during sending requests to WebApp. See \"Details\"\n"
                                "section to get more detailed information about the error.",
                                reply->errorString());
            }
            else {
                QJsonParseError error;
                auto resp = QString (reply->readAll ());
                QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8(), &error);
                if (doc.isNull ()) {
                    emit sigError   ("Error: WebApp",
                                    "Webapp error: error occured during parsing reply from WebApp",
                                    "An error occured during sending requests to WebApp. See \"Details\"\n"
                                    "section to get more detailed information about the error.",
                                    error.errorString());
                    return;
                }
                auto obj = doc.object ();
                if ("0" == obj["Res"].toString ()) {
                    changeProperty (ui->labelLoginStatus, "Status", "fail");
                    ui->labelLoginStatus->setText ("Credentionals are not valid");
                }
                else {
                    auto name = obj["Name"].toString ();
                    Credentionals::instance ().setData (email, name, hashPassw);
                    emit sigSuccess ();
                }
            }
            reply->deleteLater();
        });
    }
#endif
}

void LoginWgt::clear ()
{
    auto lineEdits = findChildren<QLineEdit *>();
    foreach (auto edit, lineEdits)
        edit->clear ();
    ui->labelLoginStatus->clear ();
}

void LoginWgt::showEvent(QShowEvent *event)
{
    ui->lineEditUsername->setFocus ();
    QWidget::showEvent(event);
}

LoginWgt::~LoginWgt()
{
    delete ui;
}
