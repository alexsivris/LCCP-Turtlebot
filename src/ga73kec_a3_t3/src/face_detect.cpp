#include "opencv2/objdetect/objdetect.hpp"
 #include "opencv2/highgui/highgui.hpp"
 #include "opencv2/imgproc/imgproc.hpp"

#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>
#include "cv_bridge/cv_bridge.h"
#include "ros/package.h"

 #include <iostream>
 #include <stdio.h>

 using namespace std;
 using namespace cv;

 /** Function Headers */
 void detectAndDisplay( Mat frame );

 /** Global variables */
 String face_cascade_name = ros::package::getPath("ga73kec_a3_t3") + "/src/resources/haarcascade_frontalface_alt.xml";
 String eyes_cascade_name = ros::package::getPath("ga73kec_a3_t3") + "/src/resources/haarcascade_eye_tree_eyeglasses.xml";
 CascadeClassifier face_cascade;
 CascadeClassifier eyes_cascade;
 string window_name = "Capture - Face detection";
 RNG rng(12345);

 ros::Subscriber sub_img;
//pointer to cvimage
 cv_bridge::CvImageConstPtr cv_ptr;

 /* save scanned data into capture object */
 void rgbCallback(const sensor_msgs::ImageConstPtr& msg)
 {

     cv_bridge::CvImageConstPtr cv_ptr;
     try
     {
       cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);

     }
     catch (cv_bridge::Exception& ex)
     {
         ROS_ERROR("cv_bridge exception: %s", ex.what());
         exit(-1);
     }


     //-- 2. Read the video stream --- USE ROS KINECT STREAM INSTEAD
     Mat frame;
     if( cv_ptr )
     {
       //while( true )
       //{
     frame = cv_ptr->image;

     //-- 3. Apply the classifier to the frame
         if( !frame.empty() )
         { detectAndDisplay( frame ); }
        // else
         //{ printf(" --(!) No captured frame -- Break!"); break; }

//         int c = waitKey(10);
//         if( (char)c == 'c' ) { break; }
        //}
     }


     cv::waitKey(30); //!!!!!!
 }

 void depthCallback(const sensor_msgs::ImageConstPtr& msg)
 {

     try
     {
         cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::TYPE_16UC1);
     }
     catch (cv_bridge::Exception& ex)
     {
         ROS_ERROR("cv_bridge exception: %s", ex.what());
         exit(-1);
     }
     //cv::imshow("DEPTH", cv_ptr->image);
     cv::waitKey(30);
 }





 /** @function main */
 int main( int argc, char** argv )
 {
     ros::init(argc, argv, "face_detect");
     ros::NodeHandle nh;
     ros::Subscriber sub =  nh.subscribe("/camera/rgb/image_rect_color", 1, rgbCallback);
     ros::Subscriber depth =  nh.subscribe("/camera/depth/image", 1, depthCallback);
     //-- 1. Load the cascades
     if( !face_cascade.load( face_cascade_name ) ){ printf("--(!)Error loading face_cascade_name \n"); return -1; };
     if( !eyes_cascade.load( eyes_cascade_name ) ){ printf("--(!)Error loading eyes_cascade_name \n"); return -1; };

   ros::spin();
   return 0;
 }





/** @function detectAndDisplay */
void detectAndDisplay( Mat frame )
{
  std::vector<Rect> faces;
  Mat frame_gray;

  cvtColor( frame, frame_gray, CV_BGR2GRAY );
  equalizeHist( frame_gray, frame_gray );
  //-- Detect faces
  face_cascade.detectMultiScale( frame_gray, faces, 1.1, 2, 0|CV_HAAR_SCALE_IMAGE, Size(30, 30) );

  for( size_t i = 0; i < faces.size(); i++ )
  {
    Point center( faces[i].x + faces[i].width*0.5, faces[i].y + faces[i].height*0.5 );
    ellipse( frame, center, Size( faces[i].width*0.5, faces[i].height*0.5), 0, 0, 360, Scalar( 255, 0, 255 ), 4, 8, 0 );

    Mat faceROI = frame_gray( faces[i] );
    std::vector<Rect> eyes;

    //-- In each face, detect eyes
    eyes_cascade.detectMultiScale( faceROI, eyes, 1.1, 2, 0 |CV_HAAR_SCALE_IMAGE, Size(30, 30) );

    for( size_t j = 0; j < eyes.size(); j++ )
     {
       Point center( faces[i].x + eyes[j].x + eyes[j].width*0.5, faces[i].y + eyes[j].y + eyes[j].height*0.5 );
       int radius = cvRound( (eyes[j].width + eyes[j].height)*0.25 );
       circle( frame, center, radius, Scalar( 255, 0, 0 ), 4, 8, 0 );
     }
  }

  //-- Show what you got

   cv::imshow("Face Detection", frame);

 }
