#ifndef ENV_METRICS_H
#define ENV_METRICS_H

#include <QScreen>
#include <QList>

namespace env
{

// holds various information about Windows metrics
//
class Metrics
{
public:
  Metrics();

  // list of displays on the system
  // the first element of returned list is the primary screen
  //
  const QList<QScreen*>& displays() const;

  // full resolution
  //
  QRect desktopGeometry() const;

private:
  QList<QScreen*> m_displays;
  QRect m_geometry;

  void getDisplays();
  void calculateGeometry();
};

}  // namespace env

#endif  // ENV_METRICS_H