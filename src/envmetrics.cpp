#include "envmetrics.h"
#include "env.h"
#include <QScreen>
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

inline int getDpi(QScreen* screen)
{
  return qRound(screen->physicalDotsPerInch());
}

Display::Display(QScreen* screen, bool primary)
    : m_adapter(screen->name()), m_monitorDevice(screen->model()), m_primary(primary),
      m_resX(0), m_resY(0), m_dpi(0), m_refreshRate(0)
{
  getSettings(screen);
  m_dpi = getDpi(screen);
}

const QString& Display::adapter() const
{
  return m_adapter;
}

const QString& Display::monitorDevice() const
{
  return m_monitorDevice;
}

bool Display::primary()
{
  return m_primary;
}

int Display::resX() const
{
  return m_resX;
}

int Display::resY() const
{
  return m_resY;
}

int Display::dpi()
{
  return m_dpi;
}

int Display::refreshRate() const
{
  return m_refreshRate;
}

QString Display::toString() const
{
  return QString("%1*%2 %3hz dpi=%4 on %5%6")
      .arg(m_resX)
      .arg(m_resY)
      .arg(m_refreshRate)
      .arg(m_dpi)
      .arg(m_adapter)
      .arg(m_primary ? " (primary)" : "");
}

void Display::getSettings(QScreen* screen)
{
  m_refreshRate = screen->refreshRate();

  QSize size = screen->size();
  m_resX     = size.width();
  m_resY     = size.height();
}

Metrics::Metrics()
{
  getDisplays();
}

const std::vector<Display>& Metrics::displays() const
{
  return m_displays;
}

QRect Metrics::desktopGeometry() const
{
  QRect r;

  for (auto* s : QGuiApplication::screens()) {
    r = r.united(s->geometry());
  }

  return r;
}

void Metrics::getDisplays()
{
  QList<QScreen*> screens = QGuiApplication::screens();
  m_displays.reserve(screens.size());

  m_displays.emplace_back(screens[0], true);
  for (qsizetype i = 1; i < screens.size(); ++i) {
    m_displays.emplace_back(screens[i], false);
  }
}

}  // namespace env
