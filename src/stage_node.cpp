#include <stage_ros2/stage_node.hpp>

#include <chrono>
#include <memory>
#include <filesystem>
#include <cstring>

StageNode::StageNode(rclcpp::NodeOptions options)
: Node("stage_ros2", options), base_watchdog_timeout_(0, 0)
{
  tf_broadcaster_stage_ = std::make_shared<tf2_ros::TransformBroadcaster>(this);
  declare_parameters();
}

StageNode::~StageNode()
{
}

void StageNode::declare_parameters()
{
  this->set_parameter(rclcpp::Parameter("use_sim_time", true));
  auto param_desc_enable_gui = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_enable_gui.description = "Enable GUI!";
  this->declare_parameter<bool>("enable_gui", true, param_desc_enable_gui);

  auto param_desc_enforce_prefixes = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_enforce_prefixes.description =
    "on true it enforces prefixes on topic even with one robot";
  this->declare_parameter<bool>("enforce_prefixes", false, param_desc_enforce_prefixes);


  auto param_desc_one_tf_tree = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_one_tf_tree.description =
    "On true: all tfs are publishe on /tf and /tf_static!";
  this->declare_parameter<bool>("one_tf_tree", false, param_desc_one_tf_tree);

  auto param_desc_use_static_transformations_ = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_use_static_transformations_.description =
    "use static transformations for sensor frames!";
  this->declare_parameter<bool>(
    "use_static_transformations", true,
    param_desc_use_static_transformations_);

  auto param_desc_watchdog_timeout = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_watchdog_timeout.description =
    "timeout after which a vehicle stopps if no command is received!";
  this->declare_parameter<double>("base_watchdog_timeout", 5, param_desc_watchdog_timeout);

  auto param_desc_is_depth_canonical = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_is_depth_canonical.description = "USE depth canonical!";
  this->declare_parameter<bool>("is_depth_canonical", true, param_desc_is_depth_canonical);

  auto param_desc_publish_ground_truth = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_publish_ground_truth.description = "publishes on true a ground truth tf!";
  this->declare_parameter<bool>("publish_ground_truth", true, param_desc_publish_ground_truth);

  auto param_desc_world_file = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_world_file.description = "USE model names!";
  this->declare_parameter<std::string>("world_file", "cave.world", param_desc_world_file);

  auto param_desc_frame_id_odom_name_ = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_frame_id_odom_name_.description =
    "odom frame name or postfix in case of multiple robots";
  this->declare_parameter<std::string>("frame_id_odom", "odom", param_desc_frame_id_odom_name_);

  auto param_desc_frame_id_world_name_ = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_frame_id_world_name_.description = "world frame name for ground truth odom data";
  this->declare_parameter<std::string>("frame_id_world", "world", param_desc_frame_id_world_name_);

  auto param_desc_frame_id_base_link_name_ = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_frame_id_base_link_name_.description =
    "base link frame name or postfix in case of multiple robots";
  this->declare_parameter<std::string>(
    "frame_id_base_link", "base_link",
    param_desc_frame_id_base_link_name_);

  auto param_desc_publish_tf = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_publish_tf.description = "bool if odom publish_tf";
  this->declare_parameter<bool>("publish_tf", true, param_desc_publish_tf);

  auto param_desc_frame_laser = rcl_interfaces::msg::ParameterDescriptor{};
  param_desc_frame_laser.description = "laser frame name";
  this->declare_parameter<std::string>("frame_laser", "laser_frame", param_desc_frame_laser);
}

void StageNode::update_parameters()
{
  double base_watchdog_timeout_sec{5.0};
  this->get_parameter("enable_gui", this->enable_gui_);
  this->get_parameter("enforce_prefixes", this->enforce_prefixes_);
  this->get_parameter("one_tf_tree", this->one_tf_tree_);
  this->get_parameter("base_watchdog_timeout", base_watchdog_timeout_sec);
  this->base_watchdog_timeout_ = rclcpp::Duration::from_seconds(base_watchdog_timeout_sec);
  this->get_parameter("is_depth_canonical", this->isDepthCanonical_);
  this->get_parameter("publish_ground_truth", this->publish_ground_truth_);
  this->get_parameter("frame_id_odom", this->frame_id_odom_name_);
  this->get_parameter("frame_id_world", this->frame_id_world_name_);
  this->get_parameter("frame_id_base_link", this->frame_id_base_link_name_);
  this->get_parameter("frame_laser", this->frame_laser_);
  this->get_parameter("publish_tf", this->publish_tf_);

  this->get_parameter("world_file", this->world_file_);
  if (!std::filesystem::exists(this->world_file_)) {
    RCLCPP_FATAL(
      this->get_logger(), "The world file %s does not exist.",
      this->world_file_.c_str());
    exit(0);
  }

  if (this->one_tf_tree_){
    RCLCPP_WARN(
      this->get_logger(), "The parameter one_tf_tree is set but deprecated and will be removed in later versions");
  }

  callback_update_parameters();

  using namespace std::chrono_literals;
  timer_update_parameter_ =
    this->create_wall_timer(1000ms, std::bind(&StageNode::callback_update_parameters, this));
}

void StageNode::callback_update_parameters()
{
  double base_watchdog_timeout_sec;
  this->get_parameter("base_watchdog_timeout", base_watchdog_timeout_sec);
  this->base_watchdog_timeout_ = rclcpp::Duration::from_seconds(base_watchdog_timeout_sec);

  this->get_parameter("use_static_transformations", use_static_transformations_);

  this->get_parameter("publish_ground_truth", this->publish_ground_truth_);
  // RCLCPP_INFO(this->get_logger(), "callback_update_parameter");
}

/**
 * Is called only ones after the simulation starts with each model
 * The function fills the vehicle vector with pointers to the stage models
 * @param mod stage model
 * @param node pointer to this class
*/
int StageNode::callback_init_stage_model(Stg::Model * mod, StageNode * node)
{
  if (dynamic_cast<Stg::ModelPosition *>(mod)) {
    Stg::ModelPosition * position = dynamic_cast<Stg::ModelPosition *>(mod);
    RCLCPP_INFO(node->get_logger(), "New Vehicle \"%s\"", mod->TokenStr().c_str());
    auto vehicle = std::make_shared<Vehicle>(
      node->vehicles_.size(),
      position->GetGlobalPose(), mod->TokenStr(), node);
    node->vehicles_.push_back(vehicle);
    vehicle->positionmodel = position;
  }

  if (dynamic_cast<Stg::ModelRanger *>(mod)) {
    Stg::ModelPosition * parent = dynamic_cast<Stg::ModelPosition *>(mod->Parent());
    for (std::shared_ptr<Vehicle> vehcile: node->vehicles_) {
      if (parent == vehcile->positionmodel) {
        auto ranger =
          std::make_shared<Vehicle::Ranger>(
          vehcile->rangers_.size() + 1,
          dynamic_cast<Stg::ModelRanger *>(mod), vehcile);
        vehcile->rangers_.push_back(ranger);
      }
    }
  }
  if (dynamic_cast<Stg::ModelCamera *>(mod)) {
    Stg::ModelPosition * parent = dynamic_cast<Stg::ModelPosition *>(mod->Parent());
    for (std::shared_ptr<Vehicle> vehcile: node->vehicles_) {
      if (parent == vehcile->positionmodel) {
        auto camera =
          std::make_shared<Vehicle::Camera>(
          vehcile->cameras_.size() + 1,
          dynamic_cast<Stg::ModelCamera *>(mod), vehcile);
        vehcile->cameras_.push_back(camera);
      }
    }
  }

  if (dynamic_cast<Stg::Model *>(mod)) {
    Stg::Model * model = dynamic_cast<Stg::Model *>(mod);
    std::string model_name(mod->TokenStr().c_str());
    if(model_name.find("obstacle") != std::string::npos){ // such that only the intended models become "objects"
      RCLCPP_INFO(node->get_logger(), "New Object \"%s\"", model_name.c_str());
      auto object = std::make_shared<Object>(
        node->objects_.size(),
        model->GetGlobalPose(), mod->TokenStr(), node);
      node->objects_.push_back(object);
      object->model = model;
    }
  }

  return 0;
}

void StageNode::publish_object_visualization(StageNode * node)
{
  visualization_msgs::msg::MarkerArray marker_array;

  for (const auto& object: node->objects_) {
    visualization_msgs::msg::Marker marker_pose;
    marker_pose.id = object->id();
    marker_pose.header.frame_id = "map";
    marker_pose.header.stamp = object->node()->sim_time_;
    marker_pose.pose.position.x = object->model->GetGlobalPose().x;
    marker_pose.pose.position.y = object->model->GetGlobalPose().y;
    marker_pose.pose.position.z = object->model->GetGeom().size.z / 2; // object origin for rviz is in its center
    marker_pose.pose.orientation = createQuaternionMsgFromYaw(object->model->GetGlobalPose().a);
    marker_pose.type = visualization_msgs::msg::Marker::CUBE;
    marker_pose.ns = "ObjectPoses";
    marker_pose.action = visualization_msgs::msg::Marker::MODIFY;
    marker_pose.scale.x = object->model->GetGeom().size.x;
    marker_pose.scale.y = object->model->GetGeom().size.y;
    marker_pose.scale.z = object->model->GetGeom().size.z;
    marker_pose.color.r = object->model->GetColor().r;
    marker_pose.color.g = object->model->GetColor().g;
    marker_pose.color.b = object->model->GetColor().b;
    marker_pose.color.a = object->model->GetColor().a;

    visualization_msgs::msg::Marker marker_text;
    marker_text.id = node->objects_.size() + object->id();
    marker_text.header.frame_id = "map";
    marker_text.header.stamp = object->node()->sim_time_;
    marker_text.pose.position = marker_pose.pose.position;
    marker_text.pose.position.z = object->model->GetGeom().size.z;
    marker_text.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
    marker_text.text = object->name();
    marker_text.ns = "ObjectNames";
    marker_text.action = visualization_msgs::msg::Marker::MODIFY;
    marker_text.scale.z = 0.2;
    marker_text.color.r = 1.0;
    marker_text.color.g = 1.0;
    marker_text.color.b = 1.0;
    marker_text.color.a = 1.0;

    marker_array.markers.push_back(marker_text);
    marker_array.markers.push_back(marker_pose);
  }
  node->pub_object_->publish(marker_array);
}


int StageNode::callback_update_stage_world(Stg::World * world, StageNode * node)
{
  // We return false to indicate that we want to be called again (an
  // odd convention, but that's the way that Stage works).
  if (!rclcpp::ok()) {
    RCLCPP_INFO(node->get_logger(), "rclcpp::ok() is false. Quitting.");
    node->world->QuitAll();
    return 1;
  }

  std::scoped_lock lock(node->msg_lock);


  node->sim_time_ = rclcpp::Time(world->SimTimeNow() * 1e3);
  // We're not allowed to publish clock==0, because it used as a special
  // value in parts of ROS, #4027.
  if (int(node->sim_time_.nanoseconds()) == 0) {
    RCLCPP_DEBUG(
      node->get_logger(), "Skipping initial simulation step, to avoid publishing clock==0");
    return 0;
  }
  // loop on the robot models
  for (size_t r = 0; r < node->vehicles_.size(); ++r) {
    auto vehicle = node->vehicles_[r];
    vehicle->check_watchdog_timeout();
    vehicle->publish_msg();
    if(vehicle->node()->publish_tf_){vehicle->publish_tf();}

    // loop on the ranger devices for the current robot
    for (auto ranger: vehicle->rangers_) {
      ranger->publish_msg();
      ranger->publish_tf();
    }


    // loop on the camera devices for the current robot
    for (auto camera: vehicle->cameras_) {
      camera->publish_msg();
      camera->publish_tf();
    }
  }

  publish_object_visualization(node);

  rosgraph_msgs::msg::Clock clock_msg;
  clock_msg.clock = node->sim_time_;
  node->clock_pub_->publish(clock_msg);
  return 0;
}

bool StageNode::cb_reset_srv(
  const std_srvs::srv::Empty::Request::SharedPtr,
  std_srvs::srv::Empty::Response::SharedPtr)
{
  RCLCPP_INFO(this->get_logger(), "Resetting stage!");
  for (auto vehicle: this->vehicles_) {
    vehicle->soft_reset();
  }
  for (const auto& object: this->objects_) {
    object->soft_reset();
  }
  return true;
}

void StageNode::cb_object_setpose_srv(
  const std::shared_ptr<stage_ros2::srv::SetObjectPose::Request> request,
  std::shared_ptr<stage_ros2::srv::SetObjectPose::Response> response)
{
  geometry_msgs::msg::Point target_pos = request->position;
  if (request->target_frame_id == "map") {
    for (const auto& object: this->objects_) {
      if (object->name() == request->name) {
        RCLCPP_INFO(this->get_logger(), "Setting Position of %s relative to map!", request->name.c_str());
        Stg::Pose pose = Stg::Pose(target_pos.x, target_pos.y, target_pos.z, request->yaw);
        object->model->SetPose(pose);
        response->result = response->SUCCEEDED;
        return;
      }      
    }
    response->erros_msg = "Could not set pose: Invalid object!";
    response->result = response->FAILED;
  }
  else if (request->target_frame_id == "robot") {  
    const auto& vehicle = this->vehicles_.front(); //assume there is just one robot
    double x = vehicle->positionmodel->GetGlobalPose().x + std::cos(vehicle->positionmodel->GetGlobalPose().a) * target_pos.x - std::sin(vehicle->positionmodel->GetGlobalPose().a) * target_pos.y;
    double y = vehicle->positionmodel->GetGlobalPose().y + std::sin(vehicle->positionmodel->GetGlobalPose().a) * target_pos.x + std::cos(vehicle->positionmodel->GetGlobalPose().a) * target_pos.y;
    double yaw = vehicle->positionmodel->GetGlobalPose().a + request->yaw;

    for (const auto& object: this->objects_) {
      if (object->name() == request->name) {
        RCLCPP_INFO(this->get_logger(), "Setting Position of %s relative to robot!", request->name.c_str());
        Stg::Pose pose = Stg::Pose(x, y, target_pos.z, yaw);
        object->model->SetPose(pose);
        response->result = response->SUCCEEDED;
        return;
      }
    }
    response->erros_msg = "Could not set pose: Invalid object!";
    response->result = response->FAILED;
  }
  else {
    response->erros_msg = "Could not set pose: Frame must be either 'map' or 'robot'!";
    response->result = response->FAILED;
  }
}

void StageNode::cb_get_dyn_objects([[maybe_unused]] const std::shared_ptr<stage_ros2::srv::GetDynObjects::Request> request,
                                                          std::shared_ptr<stage_ros2::srv::GetDynObjects::Response> response)
{
  if(objects_.empty()){
    return;
  }else{
    std::vector<std::string> msg;
    for(const auto& object: this->objects_){
      msg.push_back(object->name());
    }
    response->list = msg;
  }
}

void StageNode::init(int argc, char ** argv)
{

  this->sim_time_ = rclcpp::Time(0, 0);
  update_parameters();


  // initialize the libstage
  Stg::Init(&argc, &argv);

  if (this->enable_gui_) {
    this->world = new Stg::WorldGui(600, 400, "Stage (ROS)");
  } else {
    this->world = new Stg::World();
  }

  this->world->Load(world_file_.c_str());
  this->world->AddUpdateCallback((Stg::world_callback_t)callback_update_stage_world, this);
  this->world->ForEachDescendant((Stg::model_callback_t)callback_init_stage_model, this);
}

// Subscribe to models of interest.  Currently, we find and subscribe
// to the first 'laser' model and the first 'position' model.  Returns
// 0 on success (both models subscribed), -1 otherwise.
//
// Eventually, we should provide a general way to map stage models onto ROS
// topics, similar to Player .cfg files.
int StageNode::SubscribeModels()
{
  for (std::shared_ptr<Vehicle> vehicle: this->vehicles_) {
    // init topics and use the stage models names if there are more than one vehicle in the world
    bool use_topic_prefix = this->enforce_prefixes_ || (vehicles_.size() > 1); // a prefixes are enforced
    vehicle->init(use_topic_prefix, this->one_tf_tree_);
  }

  for (const auto& object: this->objects_) {
    object->init();
  }

  // create the clock publisher
  clock_pub_ = this->create_publisher<rosgraph_msgs::msg::Clock>("/clock", 10);

  // initialize publisher for visualization
  rclcpp::QoS qos(10);
  pub_object_ = this->create_publisher<visualization_msgs::msg::MarkerArray>("obstacle_viszualization", qos);

  // advertising reset service
  srv_reset_ = this->create_service<std_srvs::srv::Empty>(
    "reset_positions",
    [this](const std_srvs::srv::Empty::Request::SharedPtr request,
    std_srvs::srv::Empty::Response::SharedPtr response)
    {this->cb_reset_srv(request, response);});

  srv_object_setpose_ = this->create_service<stage_ros2::srv::SetObjectPose>(
    "stage/set_object_pose", std::bind(&StageNode::cb_object_setpose_srv, this,
                                std::placeholders::_1, std::placeholders::_2));

  srv_get_dyn_objects_= this->create_service<stage_ros2::srv::GetDynObjects>(
    "stage/get_dyn_objects", std::bind(&StageNode::cb_get_dyn_objects, this,
                                std::placeholders::_1, std::placeholders::_2));

  return 0;
}

bool StageNode::UpdateWorld()
{
  return this->world->UpdateAll();
}

// helper functions
geometry_msgs::msg::TransformStamped StageNode::create_transform_stamped(
  const tf2::Transform & in,
  const rclcpp::Time & timestamp, const std::string & frame_id, const std::string & child_frame_id)
{
  geometry_msgs::msg::TransformStamped out;
  out.header.stamp = timestamp;
  out.header.frame_id = frame_id;
  out.child_frame_id = child_frame_id;
  out.transform.translation.x = in.getOrigin().getX();
  out.transform.translation.y = in.getOrigin().getY();
  out.transform.translation.z = in.getOrigin().getZ();
  out.transform.rotation.w = in.getRotation().getW();
  out.transform.rotation.x = in.getRotation().getX();
  out.transform.rotation.y = in.getRotation().getY();
  out.transform.rotation.z = in.getRotation().getZ();
  return out;
}

geometry_msgs::msg::Quaternion StageNode::createQuaternionMsgFromYaw(double yaw)
{
  tf2::Quaternion q;
  q.setRPY(0, 0, yaw);
  return tf2::toMsg(q);
}
