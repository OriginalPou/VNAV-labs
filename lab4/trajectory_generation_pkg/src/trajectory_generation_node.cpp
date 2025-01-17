//
// Created by stewart on 9/20/20.
//

#include <eigen_conversions/eigen_msg.h>
#include <geometry_msgs/PoseArray.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <trajectory_msgs/MultiDOFJointTrajectory.h>

#include <mav_trajectory_generation/polynomial_optimization_linear.h>
#include <mav_trajectory_generation/trajectory.h>

#include <tf2/utils.h>
#include <tf/transform_datatypes.h>
#include <tf2_eigen/tf2_eigen.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <eigen3/Eigen/Dense>

#include <stdlib.h> 

#include <cmath>
#define PI M_PI


class WaypointFollower {
  [[maybe_unused]] ros::Subscriber currentStateSub;
  [[maybe_unused]] ros::Subscriber poseArraySub;
  ros::Publisher desiredStatePub;

  // Current state
  Eigen::Vector3d x;  // current position of the UAV's c.o.m. in the world frame
  double yaw0 = 0; // initial yaw orientation of the UAV's c.o.m. in the world frame

  ros::Timer desiredStateTimer;

  ros::Time trajectoryStartTime;
  mav_trajectory_generation::Trajectory trajectory;
  mav_trajectory_generation::Trajectory yaw_trajectory;

  void onCurrentState(nav_msgs::Odometry const& cur_state) {
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //  PART 1.1 |  16.485 - Fall 2021  - Lab 4 coding assignment (5 pts)
    // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~  ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
    // ~
    //
    //  Populate the variable x, which encodes the current world position of the
    //  UAV
    // ~~~~ begin solution
    
    geometry_msgs::Point posX = cur_state.pose.pose.position;
    x << posX.x, posX.y, posX.z;
    geometry_msgs::Quaternion R = cur_state.pose.pose.orientation;
    Eigen::Quaterniond q;
    Eigen::fromMsg(R,q);
    //initial yaw
    yaw0 = tf2::getYaw(q);
    

    // ~~~~ end solution
    // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
    // ~
    //                                 end part 1.1
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  }

  void generateOptimizedTrajectory(geometry_msgs::PoseArray const& poseArray) {
    if (poseArray.poses.size() < 1) {
      ROS_ERROR("Must have at least one pose to generate trajectory!");
      trajectory.clear();
      yaw_trajectory.clear();
      return;
    }

    if (!trajectory.empty()) return;

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //  PART 1.2 |  16.485 - Fall 2021  - Lab 4 coding assignment (35 pts)
    // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~  ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
    // ~
    //
    //  We are using the mav_trajectory_generation library
    //  (https://github.com/ethz-asl/mav_trajectory_generation) to perform
    //  trajectory optimization given the waypoints (based on the position and
    //  orientation of the gates on the race course).
    //  We will be finding the trajectory for the position and the trajectory
    //  for the yaw in a decoupled manner.
    //  In this section:
    //  1. Fill in the correct number for D, the dimension we should apply to
    //  the solver to find the positional trajectory
    //  2. Correctly populate the Vertex::Vector structure below (vertices,
    //  yaw_vertices) using the position of the waypoints and the yaw of the
    //  waypoints respectively
    //
    //  Hints:
    //  1. Use vertex.addConstraint(POSITION, position) where position is of
    //  type Eigen::Vector3d to enforce a waypoint position.
    //  2. Use vertex.addConstraint(ORIENTATION, yaw) where yaw is a double
    //  to enforce a waypoint yaw.
    //  3. Remember angle wraps around 2 pi. Be careful!
    //  4. For the ending waypoint for position use .makeStartOrEnd as seen with
    //  the starting waypoint instead of .addConstraint as you would do for the
    //  other waypoints.
    //
    // ~~~~ begin solution

    const int D = 3;  // dimension of each vertex in the trajectory
    mav_trajectory_generation::Vertex start_position(D), end_position(D);
    mav_trajectory_generation::Vertex::Vector vertices;
    mav_trajectory_generation::Vertex start_yaw(1), end_yaw(1);
    mav_trajectory_generation::Vertex::Vector yaw_vertices;

    // ============================================
    // Convert the pose array to a list of vertices
    // ============================================

    // Start from the current position and zero orientation
    using namespace mav_trajectory_generation::derivative_order;
    start_position.makeStartOrEnd(x, SNAP);
    vertices.push_back(start_position);
    start_yaw.addConstraint(ORIENTATION, 0);
    yaw_vertices.push_back(start_yaw);

    double last_yaw = 0;
    for (auto i = 0; i < poseArray.poses.size(); i++) {
      // Populate vertices (for the waypoint positions)
      geometry_msgs::Point vertexPos = poseArray.poses[i].position;
      Eigen::Vector3d pos;
      pos << vertexPos.x, vertexPos.y, vertexPos.z;
      
      ROS_INFO_STREAM("Vertex position n "<< i << "= \n" << pos);

      // Populate yaw_vertices (for the waypoint yaw angles)
      
      geometry_msgs::Quaternion YPR = poseArray.poses[i].orientation;
      tf2::Quaternion q;
      tf2::fromMsg(YPR, q);
      double yaw = tf2::getYaw(q);
      //ROS_INFO_STREAM("VERTEX YAX" << yaw);
      yaw =  yaw>=0? yaw : yaw+2*PI;
      ROS_INFO_STREAM("VERTEX YAW" << yaw);
      
      if(i< poseArray.poses.size()-1){
        // intermidiate position
        mav_trajectory_generation::Vertex int_position(D);
        int_position.addConstraint(POSITION, pos);
        vertices.push_back(int_position);

        mav_trajectory_generation::Vertex int_yaw(1);
        int_yaw.addConstraint(ORIENTATION, yaw);
        yaw_vertices.push_back(int_yaw);

      }else{
        end_position.makeStartOrEnd(pos, SNAP);
        vertices.push_back(end_position);
        end_yaw.addConstraint(ORIENTATION,last_yaw);
        yaw_vertices.push_back(end_yaw);
      }

    }

    // ~~~~ end solution
    // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
    // ~
    //                                 end part 1.2
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    // ============================================================
    // Estimate the time to complete each segment of the trajectory
    // ============================================================

    // HINT: play with these segment times and see if you can finish
    // the race course faster!
    std::vector<double> segment_times;
    const double v_max = 120.0;
    const double a_max = 50.0;
    segment_times = estimateSegmentTimes(vertices, v_max, a_max);

    // =====================================================
    // Solve for the optimized trajectory (linear optimizer)
    // =====================================================
    // Position
    const int N = 12;
    mav_trajectory_generation::PolynomialOptimization<N> opt(D);
    opt.setupFromVertices(vertices, segment_times, SNAP);
    opt.solveLinear();

    // Yaw
    mav_trajectory_generation::PolynomialOptimization<N> yaw_opt(1);
    yaw_opt.setupFromVertices(yaw_vertices, segment_times, SNAP);
    yaw_opt.solveLinear();

    // ============================
    // Get the optimized trajectory
    // ============================
    mav_trajectory_generation::Segment::Vector segments;
    //        opt.getSegments(&segments); // Unnecessary?
    opt.getTrajectory(&trajectory);
    yaw_opt.getTrajectory(&yaw_trajectory);
    trajectoryStartTime = ros::Time::now();

    ROS_INFO("Generated optimizes trajectory from %d waypoints",
             vertices.size());
  }

  void publishDesiredState(ros::TimerEvent const& ev) {
    if (trajectory.empty()) return;

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //  PART 1.3 |  16.485 - Fall 2021  - Lab 4 coding assignment (15 pts)
    // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~  ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
    // ~
    //
    //  Finally we get to send commands to our controller! First fill in
    //  properly the value for 'nex_point.time_from_start' and 'sampling_time'
    //  (hint: not 0) and after extracting the state information from our
    //  optimized trajectory, finish populating next_point.
    //
    // ~~~~ begin solution
    trajectory_msgs::MultiDOFJointTrajectoryPoint next_point;
    //next_point.time_from_start = ros::Duration(0.0);  // <--- Correct this
    next_point.time_from_start = ros::Time::now()- trajectoryStartTime;
    double sampling_time = ros::Time::now().toSec()- trajectoryStartTime.toSec();  // <--- Correct this
    if (sampling_time > trajectory.getMaxTime())
      sampling_time = trajectory.getMaxTime();

    // Getting the desired state based on the optimized trajectory we found.
    using namespace mav_trajectory_generation::derivative_order;
    Eigen::Vector3d des_position = trajectory.evaluate(sampling_time, POSITION);
    Eigen::Vector3d des_velocity = trajectory.evaluate(sampling_time, VELOCITY);
    Eigen::Vector3d des_accel =
        trajectory.evaluate(sampling_time, ACCELERATION);
    Eigen::VectorXd des_orientation =
        yaw_trajectory.evaluate(sampling_time, ORIENTATION);
    ROS_INFO("Traversed %f percent of the trajectory.",
             sampling_time / trajectory.getMaxTime() * 100);
    ROS_INFO_STREAM ("yaw at time 0 = " << yaw0);
    ROS_INFO_STREAM("des_orientation" << des_orientation);
    double des_Yaw = des_orientation[0];
    //des_Yaw = std::fmod(des_Yaw+ 2*PI, 2*PI);
    /*if (des_Yaw > PI || des_Yaw < -PI){
      des_Yaw = des_orientation[0]>=0? std::fmod(des_orientation[0], PI) : -std::fmod(abs(des_orientation[0]), PI);
    }*/
    
    ROS_INFO_STREAM("des_pos\n" << des_position);   
    // Populate next_point
    geometry_msgs::Vector3 posX;
    posX.x = des_position(0);
    posX.y = des_position(1);
    posX.z = des_position(2);
    
    geometry_msgs::Quaternion ori ;
    tf2::Quaternion myQ;
    myQ.setRPY(0, 0, des_Yaw);
    ori = tf2::toMsg(myQ);

    geometry_msgs::Transform desiredVertex;
    desiredVertex.rotation = ori;
    desiredVertex.translation = posX;

    geometry_msgs::Twist velocity;
    velocity.linear.x = des_velocity(0);
    velocity.linear.y = des_velocity(1);
    velocity.linear.z = des_velocity(2);

    velocity.angular.x = velocity.angular.y = velocity.angular.z = 0;
    geometry_msgs::Twist acceleration;
    acceleration.linear.x = des_accel(0);
    acceleration.linear.y = des_accel(1);
    acceleration.linear.z = des_accel(2);
    acceleration.angular.x = acceleration.angular.y = acceleration.angular.z = 0;
    
    next_point.transforms.resize(1);
    next_point.velocities.resize(1);
    next_point.accelerations.resize(1);

    next_point.transforms[0]=desiredVertex;
    next_point.velocities[0]=velocity;
    next_point.accelerations[0]=acceleration;


    //publishing the desired MultiDOFJointTrajectoryPoint
    desiredStatePub.publish(next_point);
    // ~~~~ end solution
    // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~
    // ~
    //                                 end part 1.3
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  }

 public:
  explicit WaypointFollower(ros::NodeHandle& nh) {
    currentStateSub = nh.subscribe(
        "/current_state", 1, &WaypointFollower::onCurrentState, this);
    poseArraySub = nh.subscribe("/desired_traj_vertices",
                                1,
                                &WaypointFollower::generateOptimizedTrajectory,
                                this);
    desiredStatePub =
        nh.advertise<trajectory_msgs::MultiDOFJointTrajectoryPoint>(
            "/desired_state", 1);
    desiredStateTimer = nh.createTimer(
        ros::Rate(5), &WaypointFollower::publishDesiredState, this);
    desiredStateTimer.start();
  }
};

int main(int argc, char** argv) {
  ros::init(argc, argv, "trajectory_generation_node");
  ros::NodeHandle nh;

  WaypointFollower waypointFollower(nh);

  ros::spin();
  return 0;
}
