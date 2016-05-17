#include "ros/ros.h"
#include "ros/package.h"
#include <ros/node_handle.h>
#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/Twist.h>
#include "std_msgs/String.h"
#include <ar_pose/ARMarker.h>
#include <ar_pose/ARMarkers.h>
//#include "../catkin_ws2/devel/include/ar_pose/ARMarker.h"
//#include "../catkin_ws2/devel/include/ar_pose/ARMarkers.h"
#include <tf2/transform_datatypes.h>
#include <tf2_ros/transform_listener.h>
#include <tf/LinearMath/Matrix3x3.h>
#include <boost/lexical_cast.hpp>
//#include <devel/include/ar_pose/ARMarker.h>



bool marker_found = true;
int last_msg = 0;
const double PI = 3.141593;

using namespace std;

void marker_callback ( const ar_pose::ARMarker::ConstPtr msg )
{

        ROS_INFO ( "Marker detected!" );
        last_msg = msg->header.stamp.sec;

}

int main ( int argc, char **argv )
{
        ros::init ( argc, argv,"pose_subscriber" );
        ROS_INFO ( "TF subscriber correctly running..." );
        ros::NodeHandle nh;
        tf2_ros::Buffer tfBuffer;
        tf2_ros::TransformListener tfListener ( tfBuffer );
        ros::Rate rate ( 1.0 );
        ros::Publisher cmd_pub = nh.advertise<std_msgs::String> ( "/uga_tum_ardrone/com",100 );
        ros::Subscriber marker_sub = nh.subscribe ( "/ar_pose_marker",1, marker_callback );
	ros::Publisher offset_pub = nh.advertise<geometry_msgs::Vector3>("/offset",100);
	
        // Save the translation and rotational offsets on three axis
        float linear_offset_X, linear_offset_Y, linear_offset_Z;
        linear_offset_X = linear_offset_Y = linear_offset_Z = 0;
        float rotational_offset_Z;
	geometry_msgs::Vector3 offset;

        tfScalar  roll, pitch, yaw;


        float target_X, target_Y,target_Z, target_yaw;
        float epsilon;
	
	nh.getParam("/ardrone_tf_controller/target_X",target_X);	// expressed in meters (def: 0.1)
	nh.getParam("/ardrone_tf_controller/target_Y",target_Y);	// expressed in meters (def: 0.1)
	nh.getParam("/ardrone_tf_controller/target_Z",target_Z);	// expressed in meters (def: 0.4)
	nh.getParam("/ardrone_tf_controller/target_yaw",target_yaw);	// expressed in degress (def: 5.0)
	nh.getParam("/ardrone_tf_controller/epsilon",epsilon);		// error variable on rotation expressed in radiants (def: 0.78)
		
        bool was_reverse = false;

        while ( nh.ok() ) {

                geometry_msgs::TransformStamped transformStamped;

                try {
                        transformStamped = tfBuffer.lookupTransform ( "ar_marker","ardrone_base_bottomcam",ros::Time ( 0 ) );
                } catch ( tf2::TransformException &ex ) {
                        ROS_WARN ( "%s",ex.what() );
                        ros::Duration ( 1.0 ).sleep();
                        continue;
                }

			
		/* Get rotation expressed in quaternion, transform it in a rotation matrix and then retrieve roll pitch and yaw
		 */
                tf::Quaternion q ( transformStamped.transform.rotation.x, transformStamped.transform.rotation.y, transformStamped.transform.rotation.z, transformStamped.transform.rotation.w );
                tf::Matrix3x3 m ( q );
                m.getRPY ( roll, pitch, yaw );
		
		
		/*
                cout << "TF : " << transformStamped.transform.translation.x << " " << transformStamped.transform.translation.y << " " << transformStamped.transform.translation.z << endl;
                cout << "yaw_rad: " << yaw << endl;
                float yaw_deg = ( float ) yaw * 180 / PI;
                cout << "yaw_deg : " << yaw_deg << endl;
		*/
		
		
		/* Consider the drone looking ahead only if its orientation is between [-45,+45] degrees
		 */
                if ( ( yaw > - epsilon ) && ( yaw < epsilon ) ) {
                        /* Multiplyer changes coordinates frame if the drone was previously revers wrt to the marker
                         */
                        int multiplyer = 1;
                        if ( was_reverse == true ) {
                                multiplyer = -1;
                        }
                        linear_offset_X = - multiplyer * transformStamped.transform.translation.x;
                        linear_offset_Y = - multiplyer * transformStamped.transform.translation.y;
                        linear_offset_Z = - abs ( transformStamped.transform.translation.z );
                        rotational_offset_Z = ( ( float ) yaw * 180 / PI );
                } else {
			linear_offset_X = - transformStamped.transform.translation.x;
                        linear_offset_Y = - transformStamped.transform.translation.y;
                        linear_offset_Z = - abs ( transformStamped.transform.translation.z );
                        rotational_offset_Z = ( ( float ) yaw * 180 / PI );
                        was_reverse = true;
                }
                
                offset.x = abs(linear_offset_X);
		offset.y = abs(linear_offset_Y);
		offset.z = abs(linear_offset_Z)	;
		offset_pub.publish(offset);

                std_msgs::String clear, autoinit,takeoff,goTo,land, moveBy, reference, maxControl, initialReachDist, stayWithinDist, stayTime;

                if ( abs ( linear_offset_X ) < target_X ) {
                        linear_offset_X = 0;
                }

                if ( abs ( linear_offset_Y ) < target_Y ) {
                        linear_offset_Y = 0;
                }

                if ( abs ( linear_offset_Z ) < target_Z ) {
                        linear_offset_Z = 0;
                }

                if ( abs ( rotational_offset_Z ) < target_yaw ) {
                        rotational_offset_Z = 0;
                }

                clear.data = "c clearCommands";

                autoinit.data = "c autoInit 500 800";
                reference.data = "setReference $POSE$";
                maxControl.data = "setMaxControl 1";
                initialReachDist.data = "setInitialReachDist 0.1";
                stayWithinDist.data = "setStayWithinDist 0.1";
                stayTime.data = "setStayTime 0.5";

                takeoff.data = "c takeoff";

                moveBy.data = "c moveByRel " + boost::lexical_cast<std::string> ( linear_offset_X ) + " " + boost::lexical_cast<std::string> ( linear_offset_Y ) + " " +
                              boost::lexical_cast<std::string> ( linear_offset_Z ) + " " + boost::lexical_cast<std::string> ( rotational_offset_Z ) ;
                land.data = "c land";

                int currentTimeInSec = ( int ) ros::Time::now().sec;

                if ( moveBy.data.compare ( "c moveByRel 0 0 0 0" ) == 0 ) {
                        ROS_INFO ( "Destination reached" );
                        cmd_pub.publish<> ( land );
                        ros::shutdown();
                }


                /* If the time stamp of the last message is equal to the current time, it means the marker has been correctly detected,
                 * otherwise it has been lost
                 */
                if ( currentTimeInSec == last_msg ) {
                        cmd_pub.publish<> ( clear );
                        cmd_pub.publish<> ( autoinit );
                        cmd_pub.publish<> ( reference );
                        cmd_pub.publish<> ( maxControl );
                        cmd_pub.publish<> ( initialReachDist );
                        cmd_pub.publish<> ( stayWithinDist );
                        cmd_pub.publish<> ( stayTime );
                        //cmd_pub.publish<> ( takeoff );
                        cmd_pub.publish<> ( moveBy );

                        //cout << "alive" << endl;
                        cout << moveBy.data << endl;
                        cout <<  endl;
                        //ros::Duration ( 1.0 ).sleep();
                } else {
                        ROS_WARN ( "Marker is lost!" );
                        cmd_pub.publish<> ( clear );
                        cmd_pub.publish<> ( autoinit );
                        cmd_pub.publish<> ( reference );
                        cmd_pub.publish<> ( maxControl );
                        cmd_pub.publish<> ( initialReachDist );
                        cmd_pub.publish<> ( stayWithinDist );
                        cmd_pub.publish<> ( stayTime );
                        //cmd_pub.publish<> ( takeoff );
                        moveBy.data = "c moveByRel 0 0 0.2 0";
                        cmd_pub.publish<> ( moveBy );
                        cout << moveBy.data << endl;
                        cout <<  endl;
                        //ros::Duration ( 1.0 ).sleep();
                }
                linear_offset_X = linear_offset_Y = linear_offset_Z = 0;
                rate.sleep();
                ros::spinOnce();
        }




        return 0;

}

