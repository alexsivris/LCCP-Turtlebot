#include <iostream>
#include "ros/ros.h"
#include "sensor_msgs/LaserScan.h"
#include "tf/transform_listener.h"

class Stopper{
	public://Tunable parameters
	const static double FORWARD_SPEED_MPS = 0.8;
	const static double MIN_SCAN_ANGLE_RAD = -30.0/180*M_PI;
	const static double MAX_SCAN_ANGLE_RAD = 30.0/180*M_PI;
	const static float MIN_PROXIMITY_RANGE_M = 0.5;	 //Should be smaller than sensor_msgs::LaserScan::range_max
	
	Stopper();
	void startMoving();

	private:
	ros::NodeHandle	node;
	ros::Publisher commandPub;	//Publisher to the robot's velocity command topic
	ros::Subscriber	laserSub;  //Subscriber to the robot's laser scan topic
	tf::TransformListener listener;
	
	bool keepMoving; //Indicates whether the robot should continue moving
	void moveForward();
	void rotateOdom(double angle);
	void rotate();
	void stepBack();
	void scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan);
};
