#include <mrs_lib/transformer.h>
#include <mrs_lib/GpsConversions.h>

namespace mrs_lib
{

// | ----------------------- Transformer ---------------------- |

/* Transformer() (constructors)//{ */

Transformer::Transformer() {
}

Transformer::~Transformer() {
}

Transformer::Transformer(const std::string& node_name, const std::string& uav_name, const double cache_timeout) {

  this->node_name_     = node_name;
  this->uav_name_      = uav_name;
  this->cache_timeout_ = cache_timeout;

  if (uav_name == "") {
    this->got_uav_name_ = false;
  } else {
    this->got_uav_name_ = true;
  }

  this->current_control_frame_ = "";

  this->tf_listener_ptr_ = std::make_unique<tf2_ros::TransformListener>(tf_buffer_, node_name);

  this->is_initialized_ = true;
}

Transformer::Transformer(const std::string& node_name, const std::string& uav_name) : Transformer(node_name, uav_name, 0.001){};

Transformer::Transformer(const std::string& node_name, const double cache_timeout) : Transformer(node_name, "", cache_timeout){};

Transformer::Transformer(const std::string& node_name) : Transformer(node_name, "", 0.001){};

Transformer::Transformer(const Transformer& other) {

  this->is_initialized_ = other.is_initialized_;
  this->node_name_      = other.node_name_;
  this->uav_name_       = other.uav_name_;
  this->cache_timeout_  = other.cache_timeout_;
  this->got_uav_name_   = other.got_uav_name_;

  {
    std::scoped_lock lock(other.mutex_current_control_frame_);

    this->current_control_frame_     = other.current_control_frame_;
    this->got_current_control_frame_ = other.got_current_control_frame_;
  }

  if (this->is_initialized_) {
    this->tf_listener_ptr_ = std::make_unique<tf2_ros::TransformListener>(tf_buffer_, this->node_name_);
  }
}

Transformer& Transformer::operator=(const Transformer& other) {

  if (this == &other) {
    return *this;
  }

  this->is_initialized_ = other.is_initialized_;
  this->node_name_      = other.node_name_;
  this->uav_name_       = other.uav_name_;
  this->cache_timeout_  = other.cache_timeout_;
  this->got_uav_name_   = other.got_uav_name_;

  {
    std::scoped_lock lock(other.mutex_current_control_frame_);

    this->current_control_frame_     = other.current_control_frame_;
    this->got_current_control_frame_ = other.got_current_control_frame_;
  }

  if (this->is_initialized_) {
    this->tf_listener_ptr_ = std::make_unique<tf2_ros::TransformListener>(tf_buffer_, this->node_name_);
  }

  return *this;
}

//}

/* transformImpl() //{ */

std::optional<geometry_msgs::PoseStamped> Transformer::transformImpl(const mrs_lib::TransformStamped& tf, const geometry_msgs::PoseStamped& what)
{
  geometry_msgs::PoseStamped ret = what;
  ret.header.frame_id = resolveFrameName(ret.header.frame_id);
  std::string latlon_frame_name = resolveFrameName(LATLON_ORIGIN);

  // if it is already in the target frame
  if (ret.header.frame_id == tf.to())
    return ret;

  // check for transformation from LAT-LON GPS
  if (tf.from() == latlon_frame_name)
  {
    // convert LAT-LON to UTM
    double utm_x, utm_y;
    mrs_lib::UTM(ret.pose.position.x, ret.pose.position.y, &utm_x, &utm_y);

    // utm_x and utm_y are now in 'utm_origin' frame
    const std::string uav_prefix = getUAVFramePrefix(tf.from());
    const std::string utm_frame_name = uav_prefix + "/utm_origin";
    ret.header.frame_id = utm_frame_name;
    ret.pose.position.x = utm_x;
    ret.pose.position.y = utm_y;

    // transform from 'utm_origin' to the desired frame
    const auto utm_origin_to_end_tf_opt = getTransform(tf.from(), tf.to(), what.header.stamp);
    if (!utm_origin_to_end_tf_opt.has_value())
      return std::nullopt;
    return doTransform(utm_origin_to_end_tf_opt.value(), ret);
  }
  // check for transformation to LAT-LON GPS
  else if (tf.to() == latlon_frame_name)
  {
    // if no UTM zone was specified by the user, we don't know which one to use...
    if (!got_utm_zone_) 
    {
      ROS_WARN_THROTTLE(1.0, "[%s]: cannot transform to latlong, missing UTM zone (did you call setCurrentLatLon()?)", node_name_.c_str());
      return std::nullopt;
    }

    // first, transform from the desired frame to 'utm_origin'
    const std::string uav_prefix = getUAVFramePrefix(tf.to());
    std::string utm_frame_name = uav_prefix + "/utm_origin";
    const auto start_to_utm_origin_tf_opt = getTransform(tf.from(), tf.to(), what.header.stamp);
    if (!start_to_utm_origin_tf_opt.has_value())
      return std::nullopt;
    const auto pose_opt = doTransform(start_to_utm_origin_tf_opt.value(), ret);
    if (!pose_opt.has_value())
      return std::nullopt;

    // now apply the nonlinear transformation from UTM to LAT-LON
    {
      std::scoped_lock lock(mutex_utm_zone_);
      double lat, lon;
      mrs_lib::UTMtoLL(what.pose.position.y, what.pose.position.x, utm_zone_, lat, lon);
      ret.pose.position.x = lat;
      ret.pose.position.y = lon;
      return ret;
    }
  }
  // if nothing interesting is requested, just do the plain old linear transformation
  else
  {
    return doTransform(tf, ret);
  }
}


//}

/* prepareMessage() //{ */

geometry_msgs::PoseStamped Transformer::prepareMessage(const mrs_msgs::ReferenceStamped& what)
{
  // create the pose message
  geometry_msgs::PoseStamped pose;
  pose.header = what.header;

  pose.pose.position.x = what.reference.position.x;
  pose.pose.position.y = what.reference.position.y;
  pose.pose.position.z = what.reference.position.z;

  const tf::Quaternion quat = tf::createQuaternionFromRPY(0, 0, what.reference.yaw);
  pose.pose.orientation.x = quat.getX();
  pose.pose.orientation.y = quat.getY();
  pose.pose.orientation.z = quat.getZ();
  pose.pose.orientation.w = quat.getW();

  return pose;
}

//}

/* postprocessMessage() //{ */

mrs_msgs::ReferenceStamped Transformer::postprocessMessage(const geometry_msgs::PoseStamped & what)
{
  mrs_msgs::ReferenceStamped ret;
  // copy the new transformed data back
  ret.reference.position.x = what.pose.position.x;
  ret.reference.position.y = what.pose.position.y;
  ret.reference.position.z = what.pose.position.z;

  tf::Quaternion quat;
  tf::quaternionMsgToTF(what.pose.orientation, quat);
  tf::Matrix3x3 m(quat);
  double roll, pitch;
  m.getRPY(roll, pitch, ret.reference.yaw);

  return ret;
}

//}

/* getTransform() //{ */

std::optional<mrs_lib::TransformStamped> Transformer::getTransform(const std::string& from_frame, const std::string& to_frame, const ros::Time& time_stamp)
{
  if (!is_initialized_)
  {
    ROS_ERROR_THROTTLE(1.0, "[%s]: Transformer: cannot provide transform, not initialized", ros::this_node::getName().c_str());
    return std::nullopt;
  }

  const std::string to_frame_resolved     = resolveFrameName(to_frame);
  const std::string from_frame_resolved   = resolveFrameName(from_frame);
  const std::string latlon_frame_resolved = resolveFrameName(LATLON_ORIGIN);

  // check for latlon transform
  if (from_frame_resolved == latlon_frame_resolved || to_frame_resolved == latlon_frame_resolved)
    return mrs_lib::TransformStamped(from_frame_resolved, to_frame_resolved, time_stamp);

  const std::string tf_frame_combined = to_frame_resolved + "->" + from_frame_resolved;

  // first try to get transform at the requested time
  try
  {
    std::scoped_lock lock(mutex_tf_buffer_);

    // get the transform
    geometry_msgs::TransformStamped transform = tf_buffer_.lookupTransform(to_frame_resolved, from_frame_resolved, time_stamp);

    // return it
    return mrs_lib::TransformStamped(from_frame_resolved, to_frame_resolved, ros::Time::now(), transform);
  }
  catch (tf2::TransformException& ex)
  {
    // this happens often -> DEBUG
    ROS_DEBUG("[%s]: Transformer: Exception caught while constructing transform from '%s' to '%s': %s", node_name_.c_str(), from_frame_resolved.c_str(),
              to_frame_resolved.c_str(), ex.what());
  }

  // otherwise get the newest one
  try
  {
    std::scoped_lock lock(mutex_tf_buffer_);

    geometry_msgs::TransformStamped transform = tf_buffer_.lookupTransform(to_frame_resolved, from_frame_resolved, ros::Time(0));

    // return it
    return mrs_lib::TransformStamped(from_frame_resolved, to_frame_resolved, ros::Time::now(), transform);
  }
  catch (tf2::TransformException& ex) {
    // this does not happen often and when it does, it should be seen
    ROS_WARN_THROTTLE(1.0, "[%s]: Transformer: Exception caught while constructing transform from '%s' to '%s': %s", node_name_.c_str(),
                      from_frame_resolved.c_str(), to_frame_resolved.c_str(), ex.what());
  }

  return std::nullopt;
}

bool Transformer::getTransform(const std::string& from_frame, const std::string& to_frame, const ros::Time& time_stamp, mrs_lib::TransformStamped& output)
{
  const auto result_opt = getTransform(from_frame, to_frame, time_stamp);
  if (result_opt.has_value())
  {
    output = result_opt.value();
    return true;
  }
  else
  {
    return false;
  }
}

//}

/* resolveFrameName() //{ */

std::string Transformer::resolveFrameName(const std::string& in) {

  if (in == "") {

    if (got_current_control_frame_) {

      std::scoped_lock lock(mutex_current_control_frame_);

      return current_control_frame_;

    } else {

      ROS_WARN_THROTTLE(
          1.0, "[%s]: Transformer: could not resolve an empty frame_id, missing the current control frame (are you calling the setCurrentControlFrame()?)",
          node_name_.c_str());

      return "";
    }
  }

  if (in.substr(0, 3) != "uav") {

    if (got_uav_name_) {

      return uav_name_ + "/" + in;

    } else {

      ROS_WARN_THROTTLE(1.0, "[%s]: Transformer: could not deduce a namespaced frame_id '%s' (did you instance the Transformer with the uav_name argument?)",
                        node_name_.c_str(), in.c_str());
    }
  }

  return in;
}

//}

/* checkUAVFramePrefix() //{ */

std::string Transformer::getUAVFramePrefix(const std::string& in)
{
  if (in.substr(0, 3) != "uav")
    std::string();
  const auto slashpos = in.find("/", 3);
  if (slashpos == std::string::npos)
    return in;
  else
    return in.substr(0, slashpos);
}

//}

/* setCurrentControlFrame() //{ */

void Transformer::setCurrentControlFrame(const std::string& in) {

  std::scoped_lock lock(mutex_current_control_frame_);

  current_control_frame_ = in;

  got_current_control_frame_ = true;
}

//}

/* setCurrentLatLon() //{ */

void Transformer::setCurrentLatLon(const double lat, const double lon) {

  double utm_x, utm_y;

  {
    std::scoped_lock lock(mutex_utm_zone_);

    LLtoUTM(lat, lon, utm_y, utm_x, utm_zone_);

    got_utm_zone_ = true;
  }
}

//}

// | --------------- TransformStamped wrapper ---------------- |

/* TransformStamped() //{ */

TransformStamped::TransformStamped() {
}

TransformStamped::TransformStamped(const std::string from_frame, const std::string to_frame, ros::Time stamp) {

  this->from_frame_ = from_frame;
  this->to_frame_   = to_frame;
  this->stamp_      = stamp;
}

TransformStamped::TransformStamped(const std::string from_frame, const std::string to_frame, ros::Time stamp,
                                   const geometry_msgs::TransformStamped transform_stamped) {

  this->from_frame_        = from_frame;
  this->to_frame_          = to_frame;
  this->transform_stamped_ = transform_stamped;
  this->stamp_             = stamp;
}

//}

/* from() //{ */

std::string TransformStamped::from(void) const {

  return from_frame_;
}

//}

/* to() //{ */

std::string TransformStamped::to(void) const {

  return to_frame_;
}

//}

/* stamp() //{ */

ros::Time TransformStamped::stamp(void) const {

  return stamp_;
}

//}

/* getTransform() //{ */

geometry_msgs::TransformStamped TransformStamped::getTransform(void) const {

  return transform_stamped_;
}

//}
}  // namespace mrs_lib
