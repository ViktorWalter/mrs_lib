#include <mrs_lib/SafetyZone/SafetyZone.h>
#include <mrs_lib/SafetyZone/lineOperations.h>

namespace mrs_lib {
    SafetyZone::SafetyZone(mrs_lib::Polygon outerBorder, std::vector<Polygon> innerObstacles,
        std::vector<PointObstacle> pointObstacles):
        outerBorder(outerBorder), innerObstacles(innerObstacles), pointObstacles(pointObstacles) {}

//    SafetyZone::SafetyZone(Eigen::MatrixXd& outerBorderMatrix,
//            std::vector<Eigen::MatrixXd>& innerObstaclesMatrixes,
//            std::vector<Eigen::MatrixXd>& pointObstaclesMatrixes):
//            outerBorder(outerBorderMatrix)
//    {
//        innerObstacles = std::vector<Polygon>{};
//        for (auto &matrix: innerObstaclesMatrixes) {
//           innerObstacles.emplace_back(matrix);
//        }
//
//        pointObstacles = std::vector<PointObstacle>{};
//        for (auto &matrix: pointObstaclesMatrixes) {
//           if (matrix.cols() != 3) {
//               throw Polygon::WrongNumberOfColumns();
//           }
//           if (matrix.rows() != 1) {
//               throw Polygon::WrongNumberOfVertices();
//           }
//
//           pointObstacles.emplace_back(Eigen::RowVector2d{matrix(0, 0), matrix(0, 1)}, matrix(0, 2));
//        }
//    }
        


    bool SafetyZone::isPointValid(const double px, const double py) {
        if (!outerBorder.isPointInside(px, py)) {
            return false;
        }
        for (auto & elem: innerObstacles) {
            if (elem.isPointInside(px, py)) {
                return false;
            }
        }

        for (auto & elem: pointObstacles) {
            if (elem.isPointInside(px, py)) {
                return false;
            }
        }
        return true;
    }


    bool SafetyZone::isPathValid(const double p1x, const double p1y, const double p2x, const double p2y) {
        if (outerBorder.doesSectionIntersect(p1x, p1y, p2x, p2y)) return false;
        for (auto & el: innerObstacles) {
            if (el.doesSectionIntersect(p1x, p1y, p2x, p2y)) return false;
        }

        for (auto & el: pointObstacles) {
            if (el.doesSectionIntersect(p1x, p1y, p2x, p2y)) return false;
        }

        return true;


    }

    Polygon SafetyZone::getBorder() {
        return outerBorder;
    }

    std::vector<Polygon> SafetyZone::getObstacles() {
      return innerObstacles;
    }

    std::vector<PointObstacle> SafetyZone::getPointObstacles() {
      return pointObstacles;
    }

    visualization_msgs::Marker SafetyZone::getMarkerMessage() {
      visualization_msgs::Marker marker;
      marker.type = visualization_msgs::Marker::LINE_LIST;
      marker.color.a = 1;
      marker.scale.x = 0.2;
      marker.color.r = 1;
      marker.color.g = 0;
      marker.color.b = 0;
      
      auto borderPoints = outerBorder.getPointMessageVector();
      for (size_t i = 0; i < borderPoints.size(); ++i) {
        marker.points.push_back(borderPoints[i]);
        marker.points.push_back(borderPoints[(i + 1) % borderPoints.size()]);
      }

      for (auto polygon: innerObstacles) {
        auto points = polygon.getPointMessageVector();

        for (size_t i = 0; i < points.size(); ++i) {
          marker.points.push_back(points[i]);
          marker.points.push_back(points[(i + 1) % points.size()]);
        } 
      }

      for (auto point: pointObstacles) {
        auto points = point.getPointMessageVector();

        for (size_t i = 0; i < points.size(); ++i) {
          marker.points.push_back(points[i]);
          marker.points.push_back(points[(i + 1) % points.size()]);
        } 
      }
      
      return marker;
    }
}
