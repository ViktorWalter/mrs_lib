#ifndef PARAM_PROVIDER_HPP
#define PARAM_PROVIDER_HPP

#include <mrs_lib/param_provider.h>

namespace mrs_lib
{
  template <typename T>
  bool ParamProvider::getParam(const std::string& param_name, T& value_out) const
  {
    try
    {
      return getParamImpl(param_name, value_out);
    }
    catch (const YAML::Exception& e)
    {
      ROS_ERROR_STREAM("[" << m_node_name << "]: YAML-CPP threw an unknown exception: " << e.what());
      return false;
    }
  }

  bool ParamProvider::getParam(const std::string& param_name, XmlRpc::XmlRpcValue& value_out) const
  {
    if (m_use_rosparam && m_nh.getParam(param_name, value_out))
      return true;

    try
    {
      const auto found_node = findYamlNode(param_name);
      if (found_node.has_value())
        ROS_WARN_STREAM("[" << m_node_name << "]: Parameter \"" << param_name << "\" of desired type XmlRpc::XmlRpcValue is only available as a static parameter, which doesn't support loading of this type.");
    }
    catch (const YAML::Exception& e)
    {
      ROS_ERROR_STREAM("[" << m_node_name << "]: YAML-CPP threw an unknown exception: " << e.what());
    }
    return false;
  }

  template <typename T>
  bool ParamProvider::getParamImpl(const std::string& param_name, T& value_out) const
  {
    {
      const auto found_node = findYamlNode(param_name);
      if (found_node.has_value())
      {
        try
        {
          // try catch is the only type-generic option...
          value_out = found_node.value().as<T>();
          return true;
        }
        catch (const YAML::BadConversion& e)
        {}
      }

    }

    if (m_use_rosparam)
      return m_nh.getParam(param_name, value_out);

    return false;
  }

  std::optional<YAML::Node> ParamProvider::findYamlNode(const std::string& param_name) const
  {
    for (const auto& yaml : m_yamls)
    {
      // Try to load the parameter sequentially as a map.
      auto cur_node_it = std::cbegin(yaml);
      // The root should always be a pam
      if (!cur_node_it->second.IsMap())
        continue;

      bool loaded = true;
      {
        constexpr char delimiter = '/';
        auto substr_start = std::cbegin(param_name);
        auto substr_end = substr_start;
        do
        {
          substr_end = std::find(substr_start, std::cend(param_name), delimiter);
          // why can't substr or string_view take iterators? :'(
          const auto start_pos = std::distance(std::cbegin(param_name), substr_start);
          const auto count = std::distance(substr_start, substr_end);
          const std::string param_substr = param_name.substr(start_pos, count);
          substr_start = substr_end+1;

          bool found = false;
          for (auto node_it = std::cbegin(cur_node_it->second); node_it != std::cend(cur_node_it->second); ++node_it)
          {
            if (node_it->first.as<std::string>() == param_substr)
            {
              cur_node_it = node_it;
              found = true;
              break;
            }
          }

          if (!found)
          {
            loaded = false;
            break;
          }
        }
        while (substr_end != std::end(param_name) && cur_node_it->second.IsMap());
      }

      if (loaded)
      {
        return cur_node_it->second;
      }
    }

    return std::nullopt;
  }
}

#endif // PARAM_PROVIDER_HPP
