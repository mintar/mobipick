/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  Copyright (c) 2018, DFKI GmbH
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Authors: Ioan Sucan, Martin Günther */

#include "mobipick_pick_n_place/fake_object_recognition.h"

#include <ros/ros.h>

// MoveIt!
#include <geometric_shapes/solid_primitive_dims.h>
#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>

// Move base
#include <actionlib/client/simple_action_client.h>
#include <move_base_msgs/MoveBaseAction.h>

// ft observer
#include <mobipick_pick_n_place/FtObserverAction.h>

// gripper
#include <control_msgs/GripperCommandAction.h>

#include <std_msgs/String.h>
#include <std_srvs/Empty.h>
#include <tf/transform_listener.h>
#include <vision_msgs/Detection3DArray.h>

#include <eigen_conversions/eigen_msg.h>

#include <sstream>

typedef actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> MoveBaseClient;

enum state
{
  ST_INIT,
  ST_ARM_TO_HOME_START,
  ST_BASE_TO_PICK,
  ST_CAPTURE_OBJ,
  ST_PICK_OBJ,
  ST_ARM_TO_TRANSPORT,
  ST_BASE_TO_HANDOVER,
  ST_ARM_TO_HANDOVER,
  ST_USER_HANDOVER,
  ST_BASE_TO_PLACE,
  ST_PLACE_OBJ,
  ST_ARM_TO_HOME_END,
  ST_BASE_TO_HOME_END,
  ST_DONE
};

struct GrapsPoseDefine
{
  Eigen::Isometry3d grasp_pose;
  std::float_t gripper_width;
};

void openGripper(trajectory_msgs::JointTrajectory &posture)
{
  posture.joint_names.resize(1);
  posture.joint_names[0] = "mobipick/gripper_finger_joint";

  posture.points.resize(1);
  posture.points[0].positions.resize(1);
  posture.points[0].positions[0] = 0.1;

  posture.points[0].effort.resize(1);
  posture.points[0].effort[0] = 30;
  posture.points[0].time_from_start.fromSec(5.0);
}

void closedGripper(trajectory_msgs::JointTrajectory &posture, std::float_t gripper_width = 0.63)
{
  posture.joint_names.resize(1);
  posture.joint_names[0] = "mobipick/gripper_finger_joint";

  posture.points.resize(1);
  posture.points[0].positions.resize(1);
  posture.points[0].positions[0] =
      gripper_width;  // closed around power drill: 0.65; fully closed: 0.76  TODO: should be 0.42 for top grasp

  posture.points[0].effort.resize(1);
  posture.points[0].effort[0] = 80;
  posture.points[0].time_from_start.fromSec(5.0);
}

moveit::planning_interface::MoveItErrorCode pick(moveit::planning_interface::MoveGroupInterface &group)
{
  std::vector<moveit_msgs::Grasp> grasps;

  // --- calculate grasps
  // this is using standard frame orientation: x forward, y left, z up, relative to object bounding box center

  std::vector<GrapsPoseDefine> grasp_poses;
  /*
    {
      // GRASP 1: pitch = pi/8  (grasp handle from upper back)
      GrapsPoseDefine grasp_pose_define;

      Eigen::AngleAxisd rotation = Eigen::AngleAxisd(M_PI/8, Eigen::Vector3d(0.0d, 1.0d, 0.0d));
      grasp_pose_define.grasp_pose = Eigen::Isometry3d::Identity();
      grasp_pose_define.grasp_pose.translate(Eigen::Vector3d(-0.05d, 0.0d, 0.01d));
      grasp_pose_define.grasp_pose.rotate(rotation);
      grasp_pose_define.gripper_width=0.03;
      grasp_poses.push_back(grasp_pose_define);

    }
  */
  {
    // GRASP 2: pitch = pi/2 (grasp top part from above)
    GrapsPoseDefine grasp_pose_define;
    Eigen::AngleAxisd rotation = Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d(0.0d, 1.0d, 0.0d));
    grasp_pose_define.grasp_pose = Eigen::Isometry3d::Identity();
    grasp_pose_define.grasp_pose.translate(Eigen::Vector3d(-0.03d, 0.0d, 0.085d));
    grasp_pose_define.grasp_pose.rotate(rotation);
    grasp_pose_define.gripper_width = 0.03;  //
    grasp_poses.push_back(grasp_pose_define);
  }
  /*
    {
      // GRASP 3: pitch = pi/2 (grasp top part from above mirrored)
      GrapsPoseDefine grasp_pose_define;
      grasp_pose_define.grasp_pose = Eigen::Isometry3d::Identity();
      grasp_pose_define.grasp_pose.translate(Eigen::Vector3d(-0.03d, 0.0d, 0.085d));
      grasp_pose_define.grasp_pose.rotate(Eigen::AngleAxisd(M_PI/2, Eigen::Vector3d(0.0d, 1.0d, 0.0d)));
      grasp_pose_define.grasp_pose.rotate(Eigen::AngleAxisd(M_PI, Eigen::Vector3d(1.0d, 0.0d, 0.0d)));
      grasp_pose_define.gripper_width=0.03; //
      grasp_poses.push_back(grasp_pose_define);
    }
  */

  for (auto &&grasp_pose : grasp_poses)
  {
    // rotate grasp pose from CAD model orientation to standard orientation (x forward, y left, z up)
    // Eigen quaternion = wxyz, not xyzw
    Eigen::Isometry3d bbox_center_rotated = Eigen::Isometry3d::Identity();
    bbox_center_rotated.rotate(Eigen::AngleAxisd(M_PI, Eigen::Vector3d(0.0d, 0.0d, 1.0d)));

    bbox_center_rotated.rotate(Eigen::AngleAxisd(-M_PI / 2, Eigen::Vector3d(1.0d, 0.0d, 0.0d)));

    // --- calculate desired pose of gripper_tcp when grasping
    // pose of power drill
    geometry_msgs::PoseStamped p;
    p.header.frame_id = "power_drill";
    tf::poseEigenToMsg(bbox_center_rotated * grasp_pose.grasp_pose, p.pose);
    ROS_DEBUG_STREAM("Grasp pose:\n" << p.pose);

    moveit_msgs::Grasp g;

    g.grasp_pose = p;
    g.grasp_quality = 1.0;
    ROS_INFO_STREAM("Grasp pose:\n" << p.pose);

    g.pre_grasp_approach.direction.vector.x = 1.0;
    g.pre_grasp_approach.direction.header.frame_id = "mobipick/gripper_tcp";
    g.pre_grasp_approach.min_distance = 0.08;
    g.pre_grasp_approach.desired_distance = 0.25;

    g.post_grasp_retreat.direction.header.frame_id = "mobipick/base_link";
    g.post_grasp_retreat.direction.vector.z = 1.0;
    g.post_grasp_retreat.min_distance = 0.1;
    g.post_grasp_retreat.desired_distance = 0.15;

    openGripper(g.pre_grasp_posture);

    closedGripper(g.grasp_posture, grasp_pose.gripper_width);

    grasps.push_back(g);
  }

  group.setSupportSurfaceName("table");
  return group.pick("power_drill", grasps);
}

moveit::planning_interface::MoveItErrorCode place(moveit::planning_interface::MoveGroupInterface &group)
{
  std::vector<moveit_msgs::PlaceLocation> loc;

  moveit::planning_interface::PlanningSceneInterface planning_scene_interface;

  // get table height
  std::vector<std::string> object_ids;
  object_ids.push_back("table");
  auto table = planning_scene_interface.getObjects(object_ids).at("table");
  double table_height =
      table.primitive_poses[0].position.z + table.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_Z] / 2.0;
  ROS_INFO("Table height: %f", table_height);

  // --- calculate desired pose of gripper_tcp when placing
  // desired pose of power drill
  geometry_msgs::PoseStamped p;

  Eigen::Isometry3d place_pose = Eigen::Isometry3d::Identity();
  place_pose.translate(Eigen::Vector3d(-0.0d, -0.9d, table_height + 0.11d));
  place_pose.rotate(Eigen::AngleAxisd(-M_PI / 2, Eigen::Vector3d(1.0d, 0.0d, 0.0d)));
  place_pose.rotate(Eigen::AngleAxisd(-M_PI / 2, Eigen::Vector3d(0.0d, 1.0d, 0.0d)));
  p.header.frame_id = "mobipick/base_link";
  tf::poseEigenToMsg(place_pose, p.pose);

  moveit_msgs::PlaceLocation g;
  g.place_pose = p;
  g.allowed_touch_objects.push_back("table");

  g.pre_place_approach.direction.header.frame_id = "mobipick/base_link";
  g.pre_place_approach.desired_distance = 0.2;
  g.pre_place_approach.direction.vector.z = -1.0;
  g.pre_place_approach.min_distance = 0.1;
  g.post_place_retreat.direction.header.frame_id = "mobipick/gripper_tcp";
  g.post_place_retreat.direction.vector.x = -1.0;
  g.post_place_retreat.desired_distance = 0.25;
  g.post_place_retreat.min_distance = 0.1;

  openGripper(g.post_place_posture);

  loc.push_back(g);
  group.setSupportSurfaceName("table");

  // add path constraints - doesn't work for place :(

  ROS_INFO_STREAM("Place at " << g.place_pose);
  auto error_code = group.place("power_drill", loc);
  group.clearPathConstraints();
  return error_code;
}

void setOrientationContraints(moveit::planning_interface::MoveGroupInterface &group, double factor_pi = 0.3)
{
  moveit_msgs::Constraints constr;

  tf::TransformListener tf_listener_;
  tf::StampedTransform transform;

  ros::Duration(1).sleep();
  try
  {
    tf_listener_.lookupTransform("mobipick/gripper_tcp", "mobipick/base_link", ros::Time(0), transform);
  }
  catch (tf::TransformException ex)
  {
    ROS_ERROR("%s", ex.what());
    ROS_INFO("Transformation not found!");
  }
  moveit_msgs::OrientationConstraint ocm;
  ocm.link_name = "mobipick/gripper_tcp";
  ocm.header.frame_id = "mobipick/base_link";

  ocm.orientation.x = -0.5;
  ocm.orientation.y = 0.5;
  ocm.orientation.z = -0.5;
  ocm.orientation.w = -0.5;
  ocm.absolute_x_axis_tolerance = factor_pi * M_PI;
  ocm.absolute_y_axis_tolerance = factor_pi * M_PI;
  ocm.absolute_z_axis_tolerance = 2.0 * M_PI;
  ocm.weight = 0.8;

  constr.orientation_constraints.push_back(ocm);

  group.setPathConstraints(constr);
}

int updatePlanningScene(moveit::planning_interface::PlanningSceneInterface &planning_scene_interface,
                        ros::NodeHandle &nh, moveit::planning_interface::MoveGroupInterface &group)
{
  // get objects from object detection
  bool found_power_drill = false;
  uint visionCounter = 0;
  while (!found_power_drill)
  {
    if (!ros::ok())
      return 0;

    vision_msgs::Detection3DArrayConstPtr detections = ros::topic::waitForMessage<vision_msgs::Detection3DArray>(
        "/mobipick/dope/detected_objects", nh, ros::Duration(30.0));
    if (!detections)
    {
      ROS_ERROR("Timed out while waiting for a message on topic detected_objects!");
      return 1;
    }

    // add objects to planning scener
    bool found_table = false;
    std::vector<moveit_msgs::CollisionObject> collision_objects;
    for (auto &&det3d : detections->detections)
    {
      if (det3d.results.empty())
      {
        ROS_ERROR("Detections3D message has empty results!");
        return 1;
      }

      if (det3d.results[0].id == ObjectID::TABLE)
        found_table = true;
      else if (det3d.results[0].id == ObjectID::POWER_DRILL)
        found_power_drill = true;

      moveit_msgs::CollisionObject co;
      co.header = detections->header;
      co.id = id_to_string(det3d.results[0].id);
      co.operation = moveit_msgs::CollisionObject::ADD;
      co.primitives.resize(1);
      co.primitives[0].type = shape_msgs::SolidPrimitive::BOX;
      co.primitives[0].dimensions.resize(
          geometric_shapes::SolidPrimitiveDimCount<shape_msgs::SolidPrimitive::BOX>::value);
      co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_X] = det3d.bbox.size.x + 0.04;
      co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_Y] = det3d.bbox.size.y + 0.04;
      co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_Z] = det3d.bbox.size.z + 0.04;
      co.primitive_poses.resize(1);
      co.primitive_poses[0] = det3d.bbox.center;

      collision_objects.push_back(co);
    }

    if (!found_table)
    {
      // Add table from MRK Lab
      moveit_msgs::CollisionObject co;
      co.header.stamp = detections->header.stamp;
      co.header.frame_id = "/map";
      co.id = id_to_string(ObjectID::TABLE);
      co.operation = moveit_msgs::CollisionObject::ADD;
      co.primitives.resize(1);
      co.primitives[0].type = shape_msgs::SolidPrimitive::BOX;
      co.primitives[0].dimensions.resize(
          geometric_shapes::SolidPrimitiveDimCount<shape_msgs::SolidPrimitive::BOX>::value);
      co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_X] = 0.80;
      co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_Y] = 0.80;
      co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_Z] = 0.73;
      co.primitive_poses.resize(1);
      co.primitive_poses[0].position.x = 12.3;
      co.primitive_poses[0].position.y = 3.8;
      co.primitive_poses[0].position.z = co.primitives[0].dimensions[shape_msgs::SolidPrimitive::BOX_Z] / 2.0;
      co.primitive_poses[0].orientation.w = 1.0;

      collision_objects.push_back(co);
    }

    if (!found_power_drill)
      ROS_INFO_THROTTLE(1.0, "Still waiting for power drill...");

    planning_scene_interface.applyCollisionObjects(collision_objects);
  }

  // detach all objects
  auto attached_objects = planning_scene_interface.getAttachedObjects();
  for (auto &&object : attached_objects)
  {
    group.detachObject(object.first);
  }
  return 1;
}

moveit::planning_interface::MoveItErrorCode move(moveit::planning_interface::MoveGroupInterface &group, double dx = 0.0,
                                                 double dy = 0.2, double dz = 0.2)
{
  robot_state::RobotState start_state(*group.getCurrentState());
  group.setStartState(start_state);

  geometry_msgs::PoseStamped actual_pose = group.getCurrentPose();
  geometry_msgs::Pose target_pose = actual_pose.pose;
  target_pose.position.x += dx;
  target_pose.position.y += dy;
  target_pose.position.z += dz;
  ROS_INFO_STREAM("Actual Pose frame: " << actual_pose.header.frame_id);
  group.setPoseTarget(target_pose);

  moveit::planning_interface::MoveGroupInterface::Plan my_plan;

  bool success = (group.plan(my_plan) == moveit::planning_interface::MoveItErrorCode::SUCCESS);

  ROS_INFO("Move planning (pose goal) %s", success ? "" : "FAILED");

  auto error_code = group.execute(my_plan);
  return error_code;
}

moveit::planning_interface::MoveItErrorCode moveToCartPose(moveit::planning_interface::MoveGroupInterface &group,
                                                           Eigen::Isometry3d cartesian_pose,
                                                           std::string base_frame = "mobipick/ur5_base_link",
                                                           std::string target_frame = "mobipick/gripper_tcp")
{
  robot_state::RobotState start_state(*group.getCurrentState());
  group.setStartState(start_state);

  geometry_msgs::PoseStamped target_pose;
  target_pose.header.frame_id = base_frame;
  tf::poseEigenToMsg(cartesian_pose, target_pose.pose);

  group.setPoseTarget(target_pose, target_frame);

  moveit::planning_interface::MoveGroupInterface::Plan my_plan;

  bool success = (group.plan(my_plan) == moveit::planning_interface::MoveItErrorCode::SUCCESS);

  ROS_INFO("Move planning (pose goal) %s", success ? "" : "FAILED");

  auto error_code = group.execute(my_plan);
  return error_code;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "mobipick_pick_n_place");
  ros::AsyncSpinner spinner(1);
  spinner.start();

  ros::NodeHandle nh;
  state task_state = ST_INIT;

  moveit::planning_interface::MoveGroupInterface group("arm");
  // MOVE BASE
  actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> move_base_ac("move_base", true);
  // FT Observer
  actionlib::SimpleActionClient<mobipick_pick_n_place::FtObserverAction> ft_observer_ac("ft_observer", true);
  // GRIPPER
  actionlib::SimpleActionClient<control_msgs::GripperCommandAction> gripper_ac("gripper_hw", true);
  // MOVE IT
  moveit::planning_interface::MoveGroupInterface::Plan plan;
  // PLANNING INTERFACE
  moveit::planning_interface::PlanningSceneInterface planning_scene_interface;
  bool handover_planned ;
  while (ros::ok())
  {
    switch (task_state)
    {
      case ST_INIT:
      {
        ROS_INFO_STREAM("ST_INIT");
        group.setPlanningTime(45.0);
        group.setPlannerId("RRTConnect");
        // wait for the move base action server to come up
        while (!move_base_ac.waitForServer(ros::Duration(5.0)))
        {
          ROS_INFO("Waiting for the move_base action server to come up");
        }
        ROS_INFO("Connected to mb action server");

        // wait for the ft observer action server to come up
        while (!ft_observer_ac.waitForServer(ros::Duration(5.0)))
        {
          ROS_INFO("Waiting for the ft_observer action server to come up");
        }
        ROS_INFO("Connected to ft observer action server");

        // wait for the gripper action server to come up
        while (!gripper_ac.waitForServer(ros::Duration(5.0)))
        {
          ROS_INFO("Waiting for the gripper action server to come up");
        }
        ROS_INFO("Connected to gripper action server");
        handover_planned = true;
        task_state = ST_ARM_TO_HOME_START;
        break;
      }
      case ST_ARM_TO_HOME_START:
      {
        ROS_INFO_STREAM("ST_ARM_TO_HOME_START");

        /* ******************* MOVE ARM TO HOME ****************************** */
        // plan
        group.setPlannerId("RRTConnect");
        group.setNamedTarget("home");
        moveit::planning_interface::MoveItErrorCode error_code = group.plan(plan);
        if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ROS_INFO("Planning to home pose SUCCESSFUL");
        }
        else
        {
          ROS_ERROR("Planning to home pose FAILED");
          return 1;
        }

        // move
        error_code = group.execute(plan);
        if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ROS_INFO("Moving to home pose SUCCESSFUL");
          task_state = ST_BASE_TO_PICK;
        }
        else
        {
          ROS_ERROR("Moving to home pose FAILED");
          return 1;
        }

        break;
      }
      case ST_BASE_TO_PICK:
      {
        ROS_INFO_STREAM("ST_BASE_TO_PICK");
        /* ******************* MOVE TO TABLE ****************************** */

        move_base_msgs::MoveBaseGoal mb_goal;
        mb_goal.target_pose.header.frame_id = "map";
        mb_goal.target_pose.header.stamp = ros::Time::now();
        /* Smart Factory
        mb_goal.target_pose.pose.position.x = 0.8;
        mb_goal.target_pose.pose.position.y = 0.1;
        mb_goal.target_pose.pose.orientation.x = -0.00512939136499;
        mb_goal.target_pose.pose.orientation.y = 0.00926916067662;
        mb_goal.target_pose.pose.orientation.z = -0.00176109502733;
        mb_goal.target_pose.pose.orientation.w = 0.999942333612;*/

        /* Berghoffstr pick pose*/
        mb_goal.target_pose.pose.position.x = 12.331;
        mb_goal.target_pose.pose.position.y = 2.995;
        mb_goal.target_pose.pose.orientation.x = 0.000;
        mb_goal.target_pose.pose.orientation.y = 0.000;
        mb_goal.target_pose.pose.orientation.z = 1.000;
        mb_goal.target_pose.pose.orientation.w = 0.000;

        ROS_INFO("Send pick base pose and wait...");
        move_base_ac.sendGoal(mb_goal);

        move_base_ac.waitForResult();

        if (move_base_ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
        {
          ROS_INFO("Moving base to pick pose SUCCESSFUL");
          task_state = ST_CAPTURE_OBJ;
        }
        else
        {
          ROS_INFO("Moving base to pick pose FAILED");
          return 1;
        }
        break;
      }
      case ST_CAPTURE_OBJ:
      {
        ROS_INFO_STREAM("ST_CAPTURE_OBJ");

        /* ********************* PLAN AND EXECUTE MOVES ********************* */

        // plan to observe the table
        group.setNamedTarget("observe100cm_right");
        moveit::planning_interface::MoveItErrorCode error_code = group.plan(plan);
        if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ROS_INFO("Planning to observation pose SUCCESSFUL");
        }
        else
        {
          ROS_ERROR("Planning to observation pose FAILED");
          return 1;
        }

        // move to observation pose
        error_code = group.execute(plan);
        if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ROS_INFO("Moving to observation pose SUCCESSFUL");
          task_state = ST_PICK_OBJ;
        }
        else
        {
          ROS_ERROR("Moving to observation pose FAILED");
          return 1;
        }
        ros::WallDuration(5.0).sleep();
        break;
      }
      case ST_PICK_OBJ:
      {
        ROS_INFO_STREAM("ST_PICK_OBJ");

        /* ********************* PICK ********************* */

        // clear octomap
        ros::ServiceClient clear_octomap = nh.serviceClient<std_srvs::Empty>("clear_octomap");
        std_srvs::Empty srv;

        // pick
        moveit::planning_interface::MoveItErrorCode error_code;
        uint pickPlanAttempts = 0;
        do
        {
          clear_octomap.call(srv);

          updatePlanningScene(planning_scene_interface, nh, group);
          error_code = pick(group);
          ++pickPlanAttempts;

          if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
          {
            ROS_INFO("Picking SUCCESSFUL");
            task_state = ST_ARM_TO_TRANSPORT;
          }
          else if ((error_code == moveit::planning_interface::MoveItErrorCode::PLANNING_FAILED ||
                    error_code == moveit::planning_interface::MoveItErrorCode::INVALID_MOTION_PLAN) &&
                   pickPlanAttempts < 10)
          {
            ROS_INFO("Planning for Picking FAILED");
          }
          else
          {
            ROS_ERROR("Picking FAILED");
            return 1;
          }

        } while ((error_code == moveit::planning_interface::MoveItErrorCode::PLANNING_FAILED ||
                  error_code == moveit::planning_interface::MoveItErrorCode::INVALID_MOTION_PLAN) &&
                 pickPlanAttempts < 10);
        break;
      }
      case ST_ARM_TO_TRANSPORT:
      {
        ROS_INFO_STREAM("ST_ARM_TO_TRANSPORT");

        /* ********************* PLAN AND EXECUTE TO TRANSPORT POSE ********************* */
        setOrientationContraints(group, 0.2);

        Eigen::Isometry3d transport_pose = Eigen::Isometry3d::Identity();
        transport_pose.translate(Eigen::Vector3d(0.3, -0.2, 0.07));
        transport_pose.rotate(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d(0.0, 1.0, 0.0)));
        transport_pose.rotate(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d(1.0, 0.0, 0.0)));

        if (moveToCartPose(group, transport_pose) == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ros::WallDuration(1.0).sleep();
          if(handover_planned)
          {
           task_state = ST_BASE_TO_HANDOVER;
          }
          else
            task_state = ST_BASE_TO_PLACE;
        }
        break;
      }
      case ST_BASE_TO_HANDOVER:
      {
        ROS_INFO_STREAM("ST_BASE_TO_HANDOVER");
        /* ********************* MOVE TO PLACE ********************* */

        move_base_msgs::MoveBaseGoal mb_goal;
        mb_goal.target_pose.header.frame_id = "map";
        mb_goal.target_pose.header.stamp = ros::Time::now();
        /* Smart Factory
        mb_goal.target_pose.pose.position.x =7.84681434123;
        mb_goal.target_pose.pose.position.y = -3.15925832165;
        mb_goal.target_pose.pose.orientation.x = 0.000360226159889;
        mb_goal.target_pose.pose.orientation.y =  3.46092411389e-05;
        mb_goal.target_pose.pose.orientation.z = -0.015630101472;
        mb_goal.target_pose.pose.orientation.w = 0.999877777014;
        */

        /* Berghoffstr. place pose*/
        mb_goal.target_pose.pose.position.x = 11.05;
        mb_goal.target_pose.pose.position.y = 3.37;
        mb_goal.target_pose.pose.orientation.x = 0.0;
        mb_goal.target_pose.pose.orientation.y = 0.0;
        mb_goal.target_pose.pose.orientation.z = 0.914895905969;
        mb_goal.target_pose.pose.orientation.w = 0.403689832967;

        ROS_INFO("Send base handover pose and wait...");
        move_base_ac.sendGoal(mb_goal);

        move_base_ac.waitForResult();

        if (move_base_ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
        {
          ROS_INFO("Moving base to handover pose SUCCESSFUL");
          task_state = ST_ARM_TO_HANDOVER;
        }
        else
        {
          ROS_INFO("Moving base to handover pose FAILED");
          return 1;
        }
        break;
      }
      case ST_ARM_TO_HANDOVER:
      {
        ROS_INFO_STREAM("ST_ARM_TO_HANDOVER");
        /* ********************* PLAN AND EXECUTE TO HAND OVER POSE ********************* */
        setOrientationContraints(group, 0.3);
        Eigen::Isometry3d hand_over_pose = Eigen::Isometry3d::Identity();
        hand_over_pose.translate(Eigen::Vector3d(0.8, -0.4, 0.2));
        hand_over_pose.rotate(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d(0.0, 1.0, 0.0)));
        hand_over_pose.rotate(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d(1.0, 0.0, 0.0)));
        hand_over_pose.rotate(Eigen::AngleAxisd(M_PI / 2, Eigen::Vector3d(0.0, 0.0, 1.0)));

        if (moveToCartPose(group, hand_over_pose) == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          task_state = ST_USER_HANDOVER;
        }
        break;
      }
      case ST_USER_HANDOVER:
      {
        ROS_INFO_STREAM("ST_USER_HANDOVER");
        /* ********************* WAIT FOR USER ********************* */
        mobipick_pick_n_place::FtObserverGoal ft_goal;

        ft_goal.threshold = 5.0;
        ft_goal.timeout = 30.0;

        ft_observer_ac.sendGoal(ft_goal);

        ft_observer_ac.waitForResult();

        if (ft_observer_ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
        {
          ROS_INFO("Detection user interaction SUCCESSFUL");
          control_msgs::GripperCommandGoal gripper_goal;

          gripper_goal.command.position = 0.1;
          gripper_goal.command.max_effort = 30.0;
          gripper_ac.sendGoal(gripper_goal);

          gripper_ac.waitForResult();

          if (gripper_ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
          {
            ROS_INFO("Gripper move SUCCESSFUL, detach all Objects");

            // detach all objects
            auto attached_objects = planning_scene_interface.getAttachedObjects();
            std::vector<std::string> objects_to_remove;
            for (auto &&object : attached_objects)
            {
              ROS_INFO_STREAM("Detach object " << object.first);
              group.detachObject(object.first);
              objects_to_remove.push_back(object.first);
            }
            planning_scene_interface.removeCollisionObjects(objects_to_remove);
            group.clearPathConstraints();
            task_state = ST_ARM_TO_HOME_END;
          }
          else
          {
            ROS_INFO("Gripper move FAILED");
            return 1;
          }
        }
        else
        {
          ROS_INFO("Detection user interaction FAILED, start placing Object");
          handover_planned = false;
          task_state = ST_ARM_TO_TRANSPORT;
        }
        break;
      }
      case ST_BASE_TO_PLACE:
      {
        ROS_INFO_STREAM("ST_BASE_TO_PLACE");
        /* ********************* MOVE TO PLACE ********************* */
        move_base_msgs::MoveBaseGoal mb_goal;
        mb_goal.target_pose.header.frame_id = "map";
        mb_goal.target_pose.header.stamp = ros::Time::now();
        /* Smart Factory
        mb_goal.target_pose.pose.position.x =7.84681434123;
        mb_goal.target_pose.pose.position.y = -3.15925832165;
        mb_goal.target_pose.pose.orientation.x = 0.000360226159889;
        mb_goal.target_pose.pose.orientation.y =  3.46092411389e-05;
        mb_goal.target_pose.pose.orientation.z = -0.015630101472;
        mb_goal.target_pose.pose.orientation.w = 0.999877777014;
        */

        /* Berghoffstr. place pose*/
        mb_goal.target_pose.pose.position.x = 12.291;
        mb_goal.target_pose.pose.position.y = 4.75;
        mb_goal.target_pose.pose.orientation.x = 0.0;
        mb_goal.target_pose.pose.orientation.y = 0.0;
        mb_goal.target_pose.pose.orientation.z = 0.0;
        mb_goal.target_pose.pose.orientation.w = 1;

        ROS_INFO("Send place base pose and wait...");
        move_base_ac.sendGoal(mb_goal);

        move_base_ac.waitForResult();

        if (move_base_ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
        {
          ROS_INFO("Moving base to place pose SUCCESSFUL");
          task_state = ST_PLACE_OBJ;
        }
        else
        {
          ROS_INFO("Moving base to place pose FAILED");
          return 1;
        }
        break;
      }
      case ST_PLACE_OBJ:
      {
        ROS_INFO_STREAM("ST_PLACE_OBJ");

        /* ********************* PLACE ********************* */

        // move(group, -0.05, -0.05, 0.2);
        ros::WallDuration(1.0).sleep();
        group.setPlannerId("PRMstar");
        ROS_INFO("Start Placing");
        // place
        uint placePlanAttempts = 0;
        moveit::planning_interface::MoveItErrorCode error_code;
        do
        {
          group.setPlanningTime(30 + 10 * placePlanAttempts);
          setOrientationContraints(group, 0.2);
          error_code = place(group);
          ++placePlanAttempts;
          if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
          {
            ROS_INFO("Placing SUCCESSFUL");
            task_state = ST_ARM_TO_HOME_END;
          }
          else if ((error_code == moveit::planning_interface::MoveItErrorCode::PLANNING_FAILED ||
                    error_code == moveit::planning_interface::MoveItErrorCode::INVALID_MOTION_PLAN ||
                    error_code == moveit::planning_interface::MoveItErrorCode::TIMED_OUT) &&
                   placePlanAttempts < 10)
          {
            ROS_INFO("Planning for Placing FAILED");
            ros::WallDuration(1.0).sleep();
            // move(group, 0.01, 0.01, -0.01); //TODO: make a random/suitable move
          }
          else
          {
            ROS_ERROR("Placing FAILED");
            return 1;
          }
        } while ((error_code == moveit::planning_interface::MoveItErrorCode::PLANNING_FAILED ||
                  error_code == moveit::planning_interface::MoveItErrorCode::INVALID_MOTION_PLAN ||
                  error_code == moveit::planning_interface::MoveItErrorCode::TIMED_OUT) &&
                 placePlanAttempts < 10);
        break;
      }
      case ST_ARM_TO_HOME_END:
      {
        ROS_INFO_STREAM("ST_ARM_TO_HOME_END");
        // plan to go home

        group.setPlannerId("RRTConnect");
        group.setStartStateToCurrentState();  // not sure why this is necessary after placing
        group.setNamedTarget("home");
        moveit::planning_interface::MoveItErrorCode error_code = group.plan(plan);
        if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ROS_INFO("Planning to home pose SUCCESSFUL");
        }
        else
        {
          ROS_ERROR("Planning to home pose FAILED");
          return 1;
        }

        // move to home
        error_code = group.execute(plan);
        if (error_code == moveit::planning_interface::MoveItErrorCode::SUCCESS)
        {
          ROS_INFO("Moving to home pose SUCCESSFUL");
          task_state = ST_BASE_TO_HOME_END;
        }
        else
        {
          ROS_ERROR("Moving to home pose FAILED");
          return 1;
        }
        break;
      }
      case ST_BASE_TO_HOME_END:
      {
       ROS_INFO_STREAM("ST_BASE_TO_HOME_END");
        /* ********************* MOVE TO PLACE ********************* */
        move_base_msgs::MoveBaseGoal mb_goal;
        mb_goal.target_pose.header.frame_id = "map";
        mb_goal.target_pose.header.stamp = ros::Time::now();
       
        /* Berghoffstr. home pose*/
        mb_goal.target_pose.pose.position.x = 17.6;
        mb_goal.target_pose.pose.position.y = 5.55;
        mb_goal.target_pose.pose.orientation.x = 0.0;
        mb_goal.target_pose.pose.orientation.y = 0.0;
        mb_goal.target_pose.pose.orientation.z = 0.68642644945;
        mb_goal.target_pose.pose.orientation.w = 0.727199236452;

        ROS_INFO("Send base home pose and wait...");
        move_base_ac.sendGoal(mb_goal);

        move_base_ac.waitForResult();

        if (move_base_ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
        {
          ROS_INFO("Moving base to home SUCCESSFUL");
          task_state = ST_DONE;
        }
        else
        {
          ROS_INFO("Moving base to home pose FAILED");
          return 1;
        }
        break;
      }
      case ST_DONE:
      {
        ROS_INFO_STREAM("ST_DONE");
        return 0;
      }
      default:
      {
        ROS_INFO_STREAM("Unknown state");
        return 1;
      }
    }
  }
  return 1;
}
