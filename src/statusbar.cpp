#include "statusbar.h"
#include "instancemanager.h"
#include "nexusinterface.h"
#include "organizercore.h"
#include "settings.h"
#include "ui_mainwindow.h"

using namespace Qt::StringLiterals;

StatusBar::StatusBar(QWidget* parent)
    : QStatusBar(parent), ui(nullptr), m_normal(new QLabel),
      m_progress(new QProgressBar), m_progressSpacer1(new QWidget),
      m_progressSpacer2(new QWidget), m_notifications(nullptr), m_update(nullptr),
      m_api(new QLabel)
{}

void StatusBar::setup(Ui::MainWindow* mainWindowUI, const Settings& settings)
{
  ui              = mainWindowUI;
  m_notifications = new StatusBarAction(ui->actionNotifications);
  m_update        = new StatusBarAction(ui->actionUpdate);

  addWidget(m_normal);

  m_progressSpacer1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  addPermanentWidget(m_progressSpacer1, 0);
  addPermanentWidget(m_progress);

  m_progressSpacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  addPermanentWidget(m_progressSpacer2, 0);

  addPermanentWidget(m_notifications);
  addPermanentWidget(m_update);
  addPermanentWidget(m_api);

  m_progress->setTextVisible(true);
  m_progress->setRange(0, 100);
  m_progress->setMaximumWidth(300);
  m_progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  m_update->set(false);
  m_notifications->set(false);

  m_api->setObjectName(u"apistats"_s);
  m_api->setToolTip(QObject::tr(
      "This tracks the number of queued Nexus API requests, as well as the "
      "remaining daily and hourly requests. The Nexus API limits you to a pool "
      "of requests per day and requests per hour. It is dynamically updated "
      "every time a request is completed. If you run out of requests, you will "
      "be unable to queue downloads, check updates, parse mod info, or even log "
      "in. Both pools must be consumed before this happens."));

  clearMessage();
  setProgress(-1);
  setAPI({}, {});

  checkSettings(settings);
}

void StatusBar::setProgress(int percent)
{
  bool visible = true;

  if (percent < 0 || percent >= 100) {
    clearMessage();
    visible = false;
  } else {
    showMessage(QObject::tr("Loading..."));
    m_progress->setValue(percent);
  }

  m_progress->setVisible(visible);
  m_progressSpacer1->setVisible(visible);
  m_progressSpacer2->setVisible(visible);
}

void StatusBar::setNotifications(bool hasNotifications)
{
  if (m_notifications) {
    m_notifications->set(hasNotifications);
  }
}

void StatusBar::setAPI(const APIStats& stats, const APIUserAccount& user)
{
  QString text;
  QString textColor;
  QString backgroundColor;

  if (user.type() == APIUserAccountTypes::None) {
    text = u"API: not logged in"_s;
  } else {
    text = QStringLiteral("API: Queued: %1 | Daily: %2 | Hourly: %3")
               .arg(stats.requestsQueued)
               .arg(user.limits().remainingDailyRequests)
               .arg(user.limits().remainingHourlyRequests);

    if (user.remainingRequests() > 500) {
      textColor       = u"white"_s;
      backgroundColor = u"darkgreen"_s;
    } else if (user.remainingRequests() > 200) {
      textColor       = u"black"_s;
      backgroundColor = u"rgb(226, 192, 0)"_s;  // yellow
    } else {
      textColor       = u"white"_s;
      backgroundColor = u"darkred"_s;
    }
  }

  m_api->setText(text);

  QString ss(uR"(
    QLabel
    {
      padding-left: 0.1em;
      padding-right: 0.1em;
      padding-top: 0;
      padding-bottom: 0;)"_s);

  if (!textColor.isEmpty()) {
    ss += QStringLiteral("\ncolor: %1;").arg(textColor);
  }

  if (!backgroundColor.isEmpty()) {
    ss += QStringLiteral("\nbackground-color: %2;").arg(backgroundColor);
  }

  ss += u"\n}"_s;

  m_api->setStyleSheet(ss);
  m_api->setAutoFillBackground(true);
}

void StatusBar::setUpdateAvailable(bool b)
{
  m_update->set(b);
}

void StatusBar::checkSettings(const Settings& settings)
{
  m_api->setVisible(!settings.interface().hideAPICounter());
}

void StatusBar::updateNormalMessage(OrganizerCore& core)
{
  QString game;

  if (core.managedGame()) {
    game = core.managedGame()->displayGameName();
  } else {
    game = tr("Unknown game");
  }

  QString instance = u"?"_s;
  if (auto i = InstanceManager::singleton().currentInstance())
    instance = i->displayName();

  QString profile = core.profileName();

  const auto s = QStringLiteral("%1 - %2 - %3").arg(game, instance, profile);

  m_normal->setText(s);
}

void StatusBar::showEvent(QShowEvent*)
{
  visibilityChanged(true);
}

void StatusBar::hideEvent(QHideEvent*)
{
  visibilityChanged(false);
}

void StatusBar::visibilityChanged(bool visible)
{
  // the central widget typically has no bottom padding because the status bar
  // is more than enough, but when it's hidden, the bottom widget (currently
  // the log) touches the bottom border of the window, which looks ugly
  //
  // when hiding the statusbar, the central widget is given the same border
  // margin as it has on the top (which is typically 6, as it's the default from
  // the qt designer)

  auto m = ui->centralWidget->layout()->contentsMargins();

  if (visible) {
    m.setBottom(0);
  } else {
    m.setBottom(m.top());
  }

  ui->centralWidget->layout()->setContentsMargins(m);
}

StatusBarAction::StatusBarAction(QAction* action)
    : m_action(action), m_icon(new QLabel), m_text(new QLabel)
{
  setLayout(new QHBoxLayout);
  layout()->setContentsMargins(0, 0, 0, 0);
  layout()->addWidget(m_icon);
  layout()->addWidget(m_text);
}

void StatusBarAction::set(bool visible)
{
  if (visible) {
    m_icon->setPixmap(m_action->icon().pixmap(16, 16));
    m_text->setText(cleanupActionText(m_action->text()));
  }

  setVisible(visible);
}

void StatusBarAction::mouseDoubleClickEvent(QMouseEvent* e)
{
  if (m_action->isEnabled()) {
    m_action->trigger();
  }
}

QString StatusBarAction::cleanupActionText(const QString& original) const
{
  QString s = original;

  static const QRegularExpression regex(u"\\&([^&])"_s);

  s.replace(regex, u"\\1"_s);  // &Item -> Item
  s.replace(u"&&"_s, u"&"_s);  // &&Item -> &Item

  if (s.endsWith("..."_L1)) {
    s = s.left(s.size() - 3);
  }

  return s;
}
