/**
 * This header defines several convenience functions for loading of ROS parameters
 * both static (e.g. from yaml files) and dynamic (using dynamic_reconfigure).
 * Please report any bugs to me (Matouš Vrba, vrbamato@fel.cvut.cz).
 * Hope you find these helpful :)
 **/
#ifndef PARAM_LOADER_H
#define PARAM_LOADER_H

#include <ros/ros.h>
#include <dynamic_reconfigure/server.h>
#include <string>
#include <iostream>
#include <boost/any.hpp>

namespace mrs_lib
{

// This flag indicates whether all compulsory parameters were successfully loaded.
// It is false if any of them was not loaded.
// You can include this flag in your main file using the extern C++ directive:
// extern bool load_successful;
// and then check it after loading the parameters.
bool load_successful = true;

// This function tries to load a parameter with name 'name' and a default value.
// You can use the flag 'optional' to not throw a ROS_ERROR when the parameter
// cannot be loaded and the flag 'print_value' to set whether the loaded
// value and name of the parameter should be printed to cout.
// If 'optional' is set to false and the parameter could not be loaded,
// the global flag 'load_successful' is set to false and a ROS_ERROR message
// is printer.
template <typename T>
T load_param(ros::NodeHandle &nh, const std::string &name, const T &default_value, bool optional = true, bool print_value = true, std::string node_name = std::string())
{
  T loaded = default_value;
  // try to load the parameter
  bool success = nh.getParam(name, loaded);
  if (success)
  {
    // if successfully loaded, everything is in order
    if (print_value)
    {
      // optionally, print its name and value
      if (node_name.empty())
        std::cout << "\t" << name << ":\t" << loaded << std::endl;
      else
        ROS_INFO_STREAM("[" << node_name << "]: parameter '" << name << "':\t" << loaded);
    }
  } else
  {
    // if it was not loaded, set the default value
    loaded = default_value;
    if (!optional)
    {
      // if the parameter was compulsory, alert the user and set the flag
      if (node_name.empty())
        ROS_ERROR("Could not load non-optional parameter %s", name.c_str());
      else
        ROS_ERROR("[%s]: Could not load non-optional parameter %s", node_name.c_str(), name.c_str());
      load_successful = false;
    } else
    {
      // otherwise everything is fine and just print the name and value
      if (node_name.empty())
        std::cout << "\t" << name << ":\t" << loaded << std::endl;
      else
        ROS_INFO_STREAM("[" << node_name << "]: parameter '" << name << "':\t" << loaded);
    }
  }
  // finally, return the resulting value
  return loaded;
}

// A convenience wrapper to enable writing commands like double param = load_param_optional(nh, "param_name", "node_name");
template <typename T>
T load_param_optional(ros::NodeHandle &nh, const std::string &name, const T &default_value, std::string node_name = std::string())
{
  return load_param(nh, name, default_value, true, true, node_name);
}

// This is just a convenience wrapper function which works like 'load_param' with 'optional' flag set to false.
template <typename T>
T load_param_compulsory(ros::NodeHandle &nh, const std::string &name, bool print_value = true, std::string node_name = std::string())
{
  return load_param(nh, name, T(), false, print_value, node_name);
}

// Some more convenience wrappers
template <typename T>
T load_param_compulsory(ros::NodeHandle &nh, const std::string &name, std::string node_name)
{
  return load_param(nh, name, T(), false, true, node_name);
}

// This class handles dynamic reconfiguration of parameters using dynamic_reconfigure server.
// Initialize this manager simply by instantiating an object of this templated class
// with the template parameter corresponding to the type of your config message, e.g. as
// DynamicReconfigureMgr<MyConfig> drmgr;
// This will automatically initialize the dynamic_reconfigure server and a callback method
// to asynchronously update the values in the config.
// Optionally, you can specify the ros NodeHandle to initialize the dynamic_reconfigure server
// and a flag 'print_values' to indicate whether to print new received values (only changed ones,
// default is true).
// The latest configuration is available through the public member 'config'. This should be
// changed externally with care since any change risks being overwritten in the next call to
// the 'dynamic_reconfigure_callback' method.
// Note that in case of a multithreaded ROS node, external mutexes _might_ be necessary
// to make access to the 'config' member thread-safe.
template <typename ConfigType>
class DynamicReconfigureMgr
{
  public:
      // this variable holds the latest received configuration
      ConfigType config;
      // initialize some stuff in the constructor
      DynamicReconfigureMgr(const ros::NodeHandle &nh = ros::NodeHandle("~"), bool print_values = true, std::string node_name = std::string())
        :  m_print_values(print_values), m_node_name(node_name), m_server(nh)
      {
        m_not_initialized = true;
        // initialize the dynamic reconfigure callback
        m_cbf = boost::bind(&DynamicReconfigureMgr<ConfigType>::dynamic_reconfigure_callback, this, _1, _2);
        m_server.setCallback(m_cbf);
      };

  private:
      bool m_print_values, m_not_initialized;
      std::string m_node_name;
      // dynamic_reconfigure server variables
      typename dynamic_reconfigure::Server<ConfigType> m_server;
      typename dynamic_reconfigure::Server<ConfigType>::CallbackType m_cbf;

      // the callback itself
      void dynamic_reconfigure_callback(ConfigType& new_config, uint32_t level)
      {
        if (m_node_name.empty())
          ROS_INFO("Dynamic reconfigure request received:");
        else
          ROS_INFO("[%s]: Dynamic reconfigure request received:", m_node_name.c_str());
        if (m_print_values)
        {
          print_changed_params(new_config);
        }
        m_not_initialized = false;
        config = new_config;
      };

      // method for printing names and values of new received parameters (prints only the changed ones)
      void print_changed_params(const ConfigType& new_config)
      {
        // Note that this part of the API is still unstable and may change! It was tested with ROS Kinetic.
        std::vector<typename ConfigType::AbstractParamDescriptionConstPtr> descrs = new_config.__getParamDescriptions__();
        for (auto &descr : descrs)
        {
          boost::any val, old_val;
          descr->getValue(new_config, val);
          descr->getValue(config, old_val);
          
          // try to guess the correct type of the parameter (these should be the only ones supported)
          int *intval;
          double *doubleval;
          bool *boolval;
          std::string *stringval;

          if (try_cast(val, intval))
          {
            if (!try_compare(old_val, intval) || m_not_initialized)
              print_value(descr->name, *intval);
          } else if (try_cast(val, doubleval))
          {
            if (!try_compare(old_val, doubleval) || m_not_initialized)
              print_value(descr->name, *doubleval);
          } else if (try_cast(val, boolval))
          {
            if (!try_compare(old_val, boolval) || m_not_initialized)
              print_value(descr->name, *boolval);
          } else if (try_cast(val, stringval))
          {
            if (!try_compare(old_val, stringval) || m_not_initialized)
              print_value(descr->name, *stringval);
          } else
          {
            print_value(descr->name, std::string("unknown dynamic reconfigure type"));
          }
        }
      }
      // helper method for parameter printing
      template <typename T>
      inline void print_value(std::string name, T val)
      {
        if (m_node_name.empty())
          std::cout << "\t" << name << ":\t" << val << std::endl;
        else
          ROS_INFO_STREAM("[" << m_node_name << "]: parameter '" << name << "':\t" << val);
      }
      // helper methods for automatic parameter value parsing
      template <typename T>
      inline bool try_cast(boost::any& val, T*& out)
      {
        return (out = boost::any_cast<T>(&val));
      };
      template <typename T>
      inline bool try_compare(boost::any& val, T*& to_what)
      {
        T* tmp;
        if ((tmp = boost::any_cast<T>(&val)))
        {
          /* std::cout << std::endl << *tmp << " vs " << *to_what << std::endl; */
          return *tmp == *to_what;
        } else
        { // the value should not change during runtime - this should never happen (but its better to be safe than sorry)
          if (m_node_name.empty())
            ROS_WARN("DynamicReconfigure value type has changed - this should not happen!");
          else
            ROS_WARN_STREAM("[" << m_node_name << "]: DynamicReconfigure value type has changed - this should not happen!");
          return false;
        }
      };
};

} // namespace mrs_lib

#endif // PARAM_LOADER_H
