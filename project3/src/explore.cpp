/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, Robert Bosch LLC.
 *  Copyright (c) 2015-2016, Jiri Horner.
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
 *   * Neither the name of the Jiri Horner nor the names of its
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
 *
 *********************************************************************/

/*! \mainpage Project3 SOA
 *
 * \section intro_sec Overview
 *
 * This Package was created to fullfill the requirements of a specific exercise.
 * A Turtlebot Burger gets spawned in a labyrinth.
 * The robot then trys to map the labyrinth with the help of gmapping and explore_lite.
 * After mapping the labyrinth, the map gets saved. Afterwards, the robot gets spawned 
 * at a random location and orientation and trys to find the labyrinths exit.
 *
 * \section install_sec Installation
 *
 * \subsection step1 Step 1: Install other needed packages
 * - gmapping
 * - hectorslam
 * - turtlebot3
 * - move_base
 *
 * \subsection step2 Step 2: Install project3 package
 * Extract project3 into \c .../src of your catkin workspace
 *
 *\subsection step3 Step 3: make the packages
 * execute \c catkin_make in the catkin folder
 *
 * \section start Starting the package
 * You can adjust simulationspeed:
 * edit \c .../project3/maps/Maze.world and set <tt>real_time_update_rate</tt>. (Default: 1000)
 * 
 * The package gets started with a bashfile.
 * 1. navigate to \c .../project3/launch
 * 2. open terminal
 * 3. set \c start_project3.sh executeable with <tt>sudo chmod a+rwx "start_project3.sh"</tt>
 * 4. run it with \c ./start_project3.sh
 *
 * \section output Expected Output
 * After starting, the robot can be observed in rviz while it maps the labyrinth.
 * When fully mapped, the map gets saved into \c .../project3/launch/mymap.yaml .
 * The programm will shutdown and restart with this map loaded in spawn the robot randomly.
 * The robot will then get a goal to an exit and try to reach it.
 *
 * \section Changes Code adjustments in explore.cpp
 * - Added more blacklisting cases
 * - Added resets of blacklist to give the robot another chance to explore some frontiers 
 * - Added gaussian noise to frotier-goals to reduce chance of pathplaning errors 
 * - Changed frontier cost function to distance based
 * - Set variable 'exploring' false when mapping is finished
 *
 * \section contribution_sec Code Contribution
 * Our code contribution lies mainly in improvments of the explore.cpp, adjustments of the 
 * pathplaning parameters and the various bashfiles.
 *
 * \section contributers Contributors
 * Stefan Haberl, Julian Katzenschwanz, Cajetan Koschat, Stephanie Kump
 */
#include <explore/explore.h>
#include <random>
#include <thread>

// Counter fuer Explore_Ende
int count_soar = 0; /**< Counts the number of times the blacklist got reset. */

inline static bool operator==(const geometry_msgs::Point& one,
                              const geometry_msgs::Point& two)
{
  double dx = one.x - two.x;
  double dy = one.y - two.y;
  double dist = sqrt(dx * dx + dy * dy);
  return dist < 0.01;
}

namespace explore
{
Explore::Explore()
  : private_nh_("~")
  , tf_listener_(ros::Duration(10.0))
  , costmap_client_(private_nh_, relative_nh_, &tf_listener_)
  , move_base_client_("move_base")
  , prev_distance_(0)
  , last_markers_count_(0)
{
  double timeout;
  double min_frontier_size;
  private_nh_.param("planner_frequency", planner_frequency_, 1.0);
  private_nh_.param("progress_timeout", timeout, 30.0);
  progress_timeout_ = ros::Duration(timeout);
  private_nh_.param("visualize", visualize_, false);
  private_nh_.param("potential_scale", potential_scale_, 1e-3);
  private_nh_.param("orientation_scale", orientation_scale_, 0.0);
  private_nh_.param("gain_scale", gain_scale_, 1.0);
  private_nh_.param("min_frontier_size", min_frontier_size, 0.5);

  search_ = frontier_exploration::FrontierSearch(costmap_client_.getCostmap(),
                                                 potential_scale_, gain_scale_,
                                                 min_frontier_size);

  if (visualize_) {
    marker_array_publisher_ =
        private_nh_.advertise<visualization_msgs::MarkerArray>("frontiers", 10);
  }

  ROS_INFO("Waiting to connect to move_base server");
  move_base_client_.waitForServer();
  ROS_INFO("Connected to move_base server");

  exploring_timer_ =
      relative_nh_.createTimer(ros::Duration(1. / planner_frequency_),
                               [this](const ros::TimerEvent&) { makePlan(); });
}

Explore::~Explore()
{
  ROS_INFO("In deconstructor!");
  //stop();
}

void Explore::visualizeFrontiers(
    const std::vector<frontier_exploration::Frontier>& frontiers)
{
  std_msgs::ColorRGBA blue;
  blue.r = 0;
  blue.g = 0;
  blue.b = 1.0;
  blue.a = 1.0;
  std_msgs::ColorRGBA red;
  red.r = 1.0;
  red.g = 0;
  red.b = 0;
  red.a = 1.0;
  std_msgs::ColorRGBA green;
  green.r = 0;
  green.g = 1.0;
  green.b = 0;
  green.a = 1.0;

  ROS_DEBUG("visualising %lu frontiers", frontiers.size());
  visualization_msgs::MarkerArray markers_msg;
  std::vector<visualization_msgs::Marker>& markers = markers_msg.markers;
  visualization_msgs::Marker m;

  m.header.frame_id = costmap_client_.getGlobalFrameID();
  m.header.stamp = ros::Time::now();
  m.ns = "frontiers";
  m.scale.x = 1.0;
  m.scale.y = 1.0;
  m.scale.z = 1.0;
  m.color.r = 0;
  m.color.g = 0;
  m.color.b = 255;
  m.color.a = 255;
  // lives forever
  m.lifetime = ros::Duration(0);
  m.frame_locked = true;

  // weighted frontiers are always sorted
  double min_cost = frontiers.empty() ? 0. : frontiers.front().cost;

  m.action = visualization_msgs::Marker::ADD;
  size_t id = 0;
  for (auto& frontier : frontiers) {
    m.type = visualization_msgs::Marker::POINTS;
    m.id = int(id);
    m.pose.position = {};
    m.scale.x = 0.1;
    m.scale.y = 0.1;
    m.scale.z = 0.1;
    m.points = frontier.points;
    if (goalOnBlacklist(frontier.centroid)) {
      m.color = red;
    } else {
      m.color = blue;
    }
    markers.push_back(m);
    ++id;
    m.type = visualization_msgs::Marker::SPHERE;
    m.id = int(id);
    m.pose.position = frontier.initial;
    // scale frontier according to its cost (costier frontiers will be smaller)
    double scale = std::min(std::abs(min_cost * 0.4 / frontier.cost), 0.5);
    m.scale.x = scale;
    m.scale.y = scale;
    m.scale.z = scale;
    m.points = {};
    m.color = green;
    markers.push_back(m);
    ++id;
  }
  size_t current_markers_count = markers.size();

  // delete previous markers, which are now unused
  m.action = visualization_msgs::Marker::DELETE;
  for (; id < last_markers_count_; ++id) {
    m.id = int(id);
    markers.push_back(m);
  }

  last_markers_count_ = current_markers_count;
  marker_array_publisher_.publish(markers_msg);
}



void Explore::makePlan()
{
  costmap_2d::Costmap2D* costmap2d = costmap_client_.getCostmap();  // SOA we copied this to print out frontier size in correct dimension

  // find frontiers
  auto pose = costmap_client_.getRobotPose();
  // get frontiers sorted according to cost
  auto frontiers = search_.searchFrom(pose.position);
  ROS_DEBUG("found %lu frontiers", frontiers.size());
  for (size_t i = 0; i < frontiers.size(); ++i) {
    ROS_DEBUG("frontier %zd cost: %f", i, frontiers[i].cost);
    ROS_DEBUG("frontier %zd size: %f", i, frontiers[i].size * costmap2d->getResolution());
  }

  if (frontiers.empty()) {
    ROS_INFO("Frontier list is empty");
    stop();
    return;
  }

  // publish frontiers as visualization markers
  if (visualize_) {
    visualizeFrontiers(frontiers);
  }

  // find non blacklisted frontier
  auto frontier =
      std::find_if_not(frontiers.begin(), frontiers.end(),
                       [this](const frontier_exploration::Frontier& f) {
                         return goalOnBlacklist(f.centroid);
                       });
  if (frontier == frontiers.end()) {
    ROS_INFO("Only blacklisted frontiers");
    bool reset = false;
    
    for(int i=0; i < frontiers.size(); i++){
      if(frontiers[i].size > 10/costmap2d->getResolution() ||
      (frontiers[i].centroid.x > 0 && frontiers[i].centroid.x < 1) && (frontiers[i].centroid.y > -6.5 && frontiers[i].centroid.y < -5.5) ||
      (frontiers[i].centroid.x > -1 && frontiers[i].centroid.x < 0) && (frontiers[i].centroid.y > 5.5 && frontiers[i].centroid.y < 6.5))
      {
        ROS_INFO("Sind in der if abfrage drinnen! Wir Resetten nichtmehr sondern sind fertig!");
        // reset = false;
      }
      else{
        ROS_INFO("Sind in der if abfrage drinnen! reset = true");
        reset = true;
      }
    }
    if((reset == true) && (count_soar < 5)){
      frontier_blacklist_.clear();
      ROS_INFO("Clear it and let's do it again!");
      count_soar++;
      ROS_INFO("Abbruch count: ");
      std::cout << count_soar << std::endl;
      makePlan();
      return;
    }
    else{
      ROS_INFO("Stop because reset false && cound_soar ");
      std::cout <<"Reset = "<<reset<<"  |||| count = "<< count_soar << std::endl;
      stop();
      return;
    }
    return;
  }
  geometry_msgs::Point target_position = frontier->centroid;

  // time out if we are not making any progress
  bool same_goal = prev_goal_ == target_position;
  prev_goal_ = target_position;
  if (!same_goal || prev_distance_ > frontier->min_distance) {
    // we have different goal or we made some progress = come closer to goal but on a luftline direction
    last_progress_ = ros::Time::now();
    prev_distance_ = frontier->min_distance;
  }
  // SOA function to blacklist frontiers out of the maze
  if (target_position.x > 5.8 || target_position.y > 5.8 || target_position.x < -5.8 || target_position.y < -5.8)
  {
    frontier_blacklist_.push_back(target_position);
    ROS_INFO("#");
    ROS_INFO("#");
    ROS_INFO("   XXXXXXXXXXXXXXXXXXXXXXX     Frontier out of the Maze");
    ROS_INFO("#");
    ROS_INFO("#");
    makePlan();
    return;
  }
  if (frontier->size > 10/costmap2d->getResolution())
  {
    frontier_blacklist_.push_back(target_position);
    ROS_INFO("#");
    ROS_INFO("#");
    ROS_INFO("   ######################     Frontier to Size to big");
    ROS_INFO("#");
    ROS_INFO("#");
    makePlan();
    return;
  } 
  if ((target_position.x > 0 && target_position.x < 1) && (target_position.y > -6.5 && target_position.y < -5.5))
  {
    frontier_blacklist_.push_back(target_position);
    ROS_INFO("#");
    ROS_INFO("#");
    ROS_INFO("   ######################     Frontier was 0.5, -6 exit");
    ROS_INFO("#");
    ROS_INFO("#");
    makePlan();
    return;
  }
  if ((target_position.x > -1 && target_position.x < 0) && (target_position.y > 5.5 && target_position.y < 6.5))
  {
    frontier_blacklist_.push_back(target_position);
    ROS_INFO("#");
    ROS_INFO("#");
    ROS_INFO("   ######################     Frontier was -0.5, 6 exit");
    ROS_INFO("#");
    ROS_INFO("#");
    makePlan();
    return;
  }
  
  // black list if we've made no progress for a long time
  if (ros::Time::now() - last_progress_ > progress_timeout_) {
    frontier_blacklist_.push_back(target_position);
    ROS_INFO("#");
    ROS_INFO("#");
    ROS_INFO("   YYYYYYYYYYYYYYYYYYYYYYYY    I WAITED TOO LONG!!!! Adding current goal to black list");
    ROS_INFO("#");
    ROS_INFO("#");
    makePlan();
    return;
  }

  // we don't need to do anything if we still pursuing the same goal
  if (same_goal) {
    return;
  }

// Gauss_rauschen

const double mean = 0.0;
const double stddev = 0.05;
//std::default_random_engine generator;
std::mt19937 generator(std::random_device{}()); 

std::normal_distribution<double> dist(mean, stddev);
  ROS_INFO("target_pos: x=%2.3f y=%2.3f", target_position.x, target_position.y);

target_position.x=target_position.x+dist(generator);
target_position.y=target_position.y+dist(generator);
  ROS_INFO("target_pos+noise: x=%2.3f y=%2.3f", target_position.x, target_position.y);


  // send goal to move_base if we have something new to pursue
  move_base_msgs::MoveBaseGoal goal;
  goal.target_pose.pose.position = target_position;
  goal.target_pose.pose.orientation.w = 1.;
  goal.target_pose.header.frame_id = costmap_client_.getGlobalFrameID();
  goal.target_pose.header.stamp = ros::Time::now();
  move_base_client_.sendGoal(
      goal, [this, target_position](
                const actionlib::SimpleClientGoalState& status,
                const move_base_msgs::MoveBaseResultConstPtr& result) {
        reachedGoal(status, result, target_position);
      });
}

bool Explore::goalOnBlacklist(const geometry_msgs::Point& goal)
{
  constexpr static size_t tolerace = 10; // default value = 5
  costmap_2d::Costmap2D* costmap2d = costmap_client_.getCostmap();

  // check if a goal is on the blacklist for goals that we're pursuing
  for (auto& frontier_goal : frontier_blacklist_) {
    double x_diff = fabs(goal.x - frontier_goal.x);
    double y_diff = fabs(goal.y - frontier_goal.y);

    if (x_diff < tolerace * costmap2d->getResolution() &&
        y_diff < tolerace * costmap2d->getResolution())
      return true;
  }
  return false;
}

void Explore::reachedGoal(const actionlib::SimpleClientGoalState& status,
                          const move_base_msgs::MoveBaseResultConstPtr&,
                          const geometry_msgs::Point& frontier_goal)
{
  ROS_DEBUG("Reached goal with status: %s", status.toString().c_str());
  if (status == actionlib::SimpleClientGoalState::ABORTED) {
    frontier_blacklist_.push_back(frontier_goal);
    ROS_INFO("#");
    ROS_INFO(" ZZZZZZ  Reached goal aborted! Adding current goal to black list");
    ROS_INFO("#");
  }

  // find new goal immediatelly regardless of planning frequency.
  // execute via timer to prevent dead lock in move_base_client (this is
  // callback for sendGoal, which is called in makePlan). the timer must live
  // until callback is executed.
  oneshot_ = relative_nh_.createTimer(
      ros::Duration(0, 0), [this](const ros::TimerEvent&) { makePlan(); },
      true);
}

void Explore::start()
{
  exploring_timer_.start();
}

void Explore::stop()
{
  move_base_client_.cancelAllGoals();
  exploring_timer_.stop();
  ROS_INFO("Exploration stopped.");
  ros::param::set("exploring" , false); // for bash script indication
  ros::shutdown();  // von SOA Team
}

}  // namespace explore

int main(int argc, char** argv)
{
  ros::init(argc, argv, "explore");
  if (ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME,
                                     ros::console::levels::Info)) { //Debug
    ros::console::notifyLoggerLevelsChanged();
  }
  explore::Explore explore;
  ros::spin();

  return 0;
}
