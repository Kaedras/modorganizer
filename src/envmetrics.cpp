#include "envmetrics.h"
#include <QGuiApplication>
#include <QScreen>

namespace env
{

Metrics::Metrics()
{
  getDisplays();
  calculateGeometry();
}

const QList<QScreen*>& Metrics::displays() const
{
  return m_displays;
}

QRect Metrics::desktopGeometry() const
{
  return m_geometry;
}

void Metrics::getDisplays()
{
  m_displays = QGuiApplication::screens();
}

void Metrics::calculateGeometry()
{
  QRect r;
  for (auto* s : m_displays) {
    r = r.united(s->geometry());
  }
  m_geometry = r;
}

}  // namespace env
