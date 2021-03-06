#include "deadreckoning.h"
#include "../../utilities.h"


/**
 * @brief Wrapper to create an SDL_Color structure from RGB components.
 *
 * @param r The Red component (between 0 and 255).
 * @param g The Green component (between 0 and 255).
 * @param b The Blue component (between 0 and 255).
 * @return The new SDL_Color structure.
 */
static SDL_Color createColor(int r, int g, int b)
{
    SDL_Color color = {r, g, b};
    return color;
}

/**
 * @brief Sets a pixel of an SDL Surface to a specific color.
 *
 * The surface should be locked (see SDL_LockSurface()) before calling this function.
 * This function comes from the SDL documentation.
 *
 * @param surface The SDL Surface.
 * @param x The x-coordinate of the pixel to set.
 * @param y The y-coordinate of the pixel to set.
 * @param pixel The color to set to the pixel, as a 32-bits unsigned integer (use SDL_MapRGB() / SDL_MapRGBA() to create it).
 * @param check True if the provided coordinates must be checked before setting the pixel. If check is False and invalid coordinates are set, this might cause a segfault.
 * @return True if the pixel was successfully set.
 */
static bool putPixel(SDL_Surface *surface, int x, int y, Uint32 pixel, bool check=true)
{
    if (check)
    {
        if (x < 0 || y < 0 || x >= surface->w || y >= surface->h)
            return false;
    }

    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp)
    {
        case 1:
            *p = pixel;
            break;

        case 2:
            *(Uint16 *)p = pixel;
            break;

        case 3:
            if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
            {
                p[0] = (pixel >> 16) & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = pixel & 0xff;
            }
            else
            {
                p[0] = pixel & 0xff;
                p[1] = (pixel >> 8) & 0xff;
                p[2] = (pixel >> 16) & 0xff;
            }
            break;

        case 4:
            *(Uint32 *)p = pixel;
            break;
    }

    return true;
}

/**
 * @brief Draw a line segment on a SDL Surface.
 *
 * The surface should be locked (see SDL_LockSurface()) before calling this function.
 * The line has a thickness of one pixel.
 * Provided coordinates can be outside the surface area.
 * The function comes from here: http://stackoverflow.com/questions/11737988/how-to-draw-a-line-using-sdl-without-using-external-libraries
 *
 * @param surf The SDL Surface to draw on.
 * @param x1 The x-coordinate of the first pixel of the line segment.
 * @param y1 The y-coordinate of the first pixel of the line segment.
 * @param x2 The x-coordinate of the last pixel of the line segment.
 * @param y2 The y-coordinate of the last pixel of the line segment.
 * @param color The color to use to draw the line.
 */
static void drawLine(SDL_Surface *surf, float x1, float y1, float x2, float y2, SDL_Color color)
{
    Uint32 pixel = SDL_MapRGB(surf->format, color.r, color.g, color.b);

    SDL_LockSurface(surf);

    //Bresenham's line algorithm
    const bool steep = (fabs(y2 - y1) > fabs(x2 - x1));
    if(steep)
    {
        std::swap(x1, y1);
        std::swap(x2, y2);
    }

    if(x1 > x2)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }

    const float dx = x2 - x1;
    const float dy = fabs(y2 - y1);

    float error = dx / 2.0f;
    const int ystep = (y1 < y2) ? 1 : -1;
    int y = (int)y1;

    const int maxX = (int)x2;

    for(int x=(int)x1; x<maxX; x++)
    {
        if(steep)
            putPixel(surf, y,x, pixel);
        else
            putPixel(surf, x,y, pixel);

        error -= dy;
        if(error < 0)
        {
            y += ystep;
            error += dx;
        }
    }

    SDL_UnlockSurface(surf);
}

/**
 * @brief Creates internal storage and cleans it.
 */
void Grid::init()
{
    updateSize();
    m_data = new ProbabilisticPoint*[m_height*m_width];
    //TODO check if m_data != NULL
    memset(m_data, 0, sizeof(ProbabilisticPoint*)*m_height*m_width);
    
    //ROS_INFO("(%d x %d) = %d", m_width, m_height, m_height*m_width);
}

/**
 * @brief Frees internal storage.
 */
void Grid::empty()
{
    if (m_data != NULL)
    {
        int n = m_width*m_height;
        for (int i=0 ; i < n ; i++)
        {
            if (m_data[i] != NULL)
                delete m_data[i];
        }
        delete m_data;
        m_data = NULL;
    }
}

/**
 * @brief Rounds the coordinates of the upper-left and lower-right corners and updates the height and width.
 */
void Grid::updateSize()
{
    m_minX = m_precision * round(m_minX / m_precision);
    m_minY = m_precision * round(m_minY / m_precision);
    m_maxX = m_precision * round(m_maxX / m_precision);
    m_maxY = m_precision * round(m_maxY / m_precision);
    m_height = round((m_maxY - m_minY) / m_precision)+1;
    m_width = round((m_maxX - m_minX) / m_precision)+1;
}

/**
 * @brief Accesses the probability of a point at given grid coordinates.
 *
 * @param ix The x-coordinate of the point to access.
 * @param iy The y-coordinate of the point to access.
 * @return The obstacle probability  at this point, between 0 and 1, negative means unknown.
 */
double Grid::_get(int ix, int iy)
{
    if (ix < 0 || iy < 0 || ix >= m_width || iy >= m_height)
        return -1;
    
    int k = ix*m_height+iy;
    int minTime = ros::Time::now().toSec() - m_ttl.toSec();
    if (m_data[k] == NULL || m_data[k]->t.toSec() < minTime)
        return -1;
    
    return m_data[k]->p;
}

/**
 * @brief Standard constructor.
 *
 * Creates an empty Grid mapped at specified coordinates in the real world.
 *
 * @param precision The precision of the discretization, in m / unit.
 * @param ttl The Time To Live of a point in the Grid.
 * @param minX The x-coordinate of the upper-left corner of the Grid in the real world.
 * @param maxX The x-coordinate of the lower-right corner of the Grid in the real world.
 * @param minY The y-coordinate of the upper-left corner of the Grid in the real world.
 * @param maxY The y-coordinate of the lower-right corner of the Grid in the real world.
 * @param resizeable True if the grid can be automatically resized to integrate new points. !WARNING! This is an untested feature.
 */
Grid::Grid(double precision, ros::Duration ttl, double minX, double maxX, double minY, double maxY, bool resizeable):
    m_precision(precision), m_ttl(ttl), m_minX(minX), m_maxX(maxX), m_minY(minY), m_maxY(maxY), m_resizeable(resizeable)
{
    init();
}

/**
 * @brief Copy constructor.
 *
 * Creates an EMPTY Grid with the same settings as the one provided.
 *
 * @param grid The Grid to copy.
 */
Grid::Grid(const Grid& grid):
    m_precision(grid.m_precision), m_ttl(grid.m_ttl), m_minX(grid.m_minX), m_maxX(grid.m_maxX), m_minY(grid.m_minY), m_maxY(grid.m_maxY), m_resizeable(grid.m_resizeable)
{
    init();            
}

/**
 * @brief Assignment operator.
 *
 * Empties the Grid and assigns the same settings as the one provided. The Grid is EMPTY after the operation and needs to be filled again.
 *
 * @param grid The Grid to copy.
 * @return This Grid.
 */
Grid& Grid::operator=(const Grid& grid)
{
    empty();
    m_precision = grid.m_precision;
    m_ttl = grid.m_ttl;
    m_minX = grid.m_minX;
    m_maxX = grid.m_maxX;
    m_minY = grid.m_minY;
    m_maxY = grid.m_maxY;
    m_resizeable = grid.m_resizeable;
    init();
    return *this;
}

/**
 * @brief Destructor.
 *
 * Frees internal storage.
 */
Grid::~Grid()
{
    empty();
}

/**
 * @brief Gets the Grid precision, in m / unit.
 */
double Grid::precision() const
{
    return m_precision;
}

/**
 * @brief Gets x-coordinate of the upper-left corner of the Grid in the real world.
 */
double Grid::minX() const
{
    return m_minX;
}

/**
 * @brief Gets y-coordinate of the upper-left corner of the Grid in the real world.
 */
double Grid::minY() const
{
    return m_minY;
}

/**
 * @brief Adds a new point in the Grid.
 *
 * @param x The x-coordinate of the point in the real world.
 * @param y The y-coordinate of the point in the real world.
 * @param t The time stamp of the point.
 * @param p The obstacle probability  at this point.
 * @return True if the point was successfully added.
 */
bool Grid::addPoint(double x, double y, ros::Time t, double p)
{
    ProbabilisticPoint point = {x, y, t, p};
    return addPoint(point);
}

/**
 * @brief Adds a new point in the Grid.
 *
 * @param point The point to add, with coordinates expressed in the real world.
 * @return True if the point was successfully added.
 */
bool Grid::addPoint(ProbabilisticPoint point)
{
    double x = m_precision * round(point.x / m_precision);
    double y = m_precision * round(point.y / m_precision);
    
    //ROS_INFO("Add point to grid at (%.3f, %.3f)", x, y);

    if (x < m_minX || x > m_maxX || y < m_minY || y > m_maxX)
    {
        //ROS_INFO("Out of grid: (%.3f, %.3f)", x, y);
        if (!m_resizeable)
            return false;

        //ROS_INFO("Resizing grid");

        int xShift = 0;
        if (x < m_minX)
        {
            xShift = round((m_minX - x) / m_precision);
            m_minX = x;
        }
        else if (x > m_maxX)
            m_maxX = std::max(m_maxX+1, x);
        
        int yShift = 0;
        if (y < m_minY)
        {
            yShift = round((m_minY - y) / m_precision);
            m_minY = y;
        }
        else if (y > m_maxY)
            m_maxY = std::max(m_maxY+1, y);
        
        int prevWidth = m_width;
        int prevHeight = m_height;
        updateSize();
        ProbabilisticPoint** newData = new ProbabilisticPoint*[m_height*m_width];
        memset(newData, 0, sizeof(ProbabilisticPoint*)*m_height*m_width);
        
        for (int i=0 ; i < prevWidth ; i++)
            memcpy(&(newData[(i+xShift)*m_height+yShift]), &(m_data[prevHeight*i]), sizeof(ProbabilisticPoint*)*prevHeight);
        
        delete m_data;
        m_data = newData;
    }
    
    int ix = round((x-m_minX) / m_precision);
    int iy = round((y-m_minY) / m_precision);
    //ROS_INFO("Grid coord: (%d, %d)", ix, iy);
    //ROS_INFO("(%d x %d)", m_width, m_height);
    
    int k = ix*m_height+iy;
    //ROS_INFO("%d", k);
    //ROS_INFO("%lu", sizeof(m_data)/sizeof(std::list<BooleanPoint>*));
    
    if (m_data[k] == NULL)
        m_data[k] = new ProbabilisticPoint;
    else if (point.t == m_data[k]->t)
        point.p = 1 - (1-point.p)*(1-m_data[k]->p);
    *(m_data[k]) = point;

    //ROS_INFO("Added to grid");
    return true;
}

/**
 * @brief Gets the obstacle probability at a given point.
 *
 * @param x The x-coordinate of the point in the real world.
 * @param y The y-coordinate of the point in the real world.
 * @return The obstacle probability  at this point, between 0 and 1, negative means unknown.
 */
double Grid::get(double x, double y)
{
    int ix = round((x-m_minX) / m_precision);
    int iy = round((y-m_minY) / m_precision);
    return _get(ix, iy);
}

/**
 * @brief Gets obstacle probabilities for all points in the Grid.
 *
 * @param width Pointer to a variable which will receive the width of the Grid in Grid units, can be NULL.
 * @param height Pointer to a variable which will receive the height of the Grid in Grid units, can be NULL.
 * @param scale Pointer to a variable which will receive the scale of the grid in m / unit, can be NULL.
 * @return Pointer to newly allocated data (must be freed with delete when not needed anymore) containing the obstacles probabilities
 * (between 0 and 1, negative means unknown) as a 1D array arranged in column-major order.
 */
double* Grid::getAll(int* width, int *height, double *scale) const
{
    double *data = new double[m_width*m_height];
    if (data == NULL)
    {
        ROS_ERROR("Unable to create double array (%dx%d)", m_width, m_height);
        return NULL;
    }
    
    for (int x=0 ; x < m_width ; x++)
    {
        int k = x * m_height;
        for (int y=0 ; y < m_height ; y++)
        {
            ProbabilisticPoint *pt = m_data[k+y];
            data[k+y] = pt != NULL ? pt->p : -1;
        }
    }
    if (width != NULL)
        *width = m_width;
    if (height != NULL)
        *height = m_height;
    if (scale != NULL)
        *scale = m_precision;
    return data;
}

/**
 * @brief Draws the Grid on an SDL Surface.
 *
 * @param w The surface's width.
 * @param h The surface's height.
 * @param minX The real world x-coordinate of the point which should be mapped to the surface's upper-left pixel.
 * @param maxX The real world x-coordinate of the point which should be mapped to the surface's lower-right pixel.
 * @param minY The real world y-coordinate of the point which should be mapped to the surface's upper-left pixel.
 * @param maxY The real world y-coordinate of the point which should be mapped to the surface's lower-right pixel.
 * @param surf The surface to draw on, can be NULL, in this case it will be created (deletion is caller's job - see SDL_FreeSurface()).
 * @return The surface with the Grid drawn on it.
 */
SDL_Surface* Grid::draw(int w, int h, double minX, double maxX, double minY, double maxY, SDL_Surface *surf)
{
    //ROS_INFO("Drawing grid");
    if (surf == NULL)
        surf = SDL_CreateRGBSurface(SDL_HWSURFACE, w, h, 32,0,0,0,0);
    if (surf == NULL)
        return NULL;
    
    //ROS_INFO("Surface created");
    SDL_LockSurface(surf);
    for (int y=0 ; y < h ; y++)
    {
        double fy = (h-y) * (maxY-minY) / h + minY;
        for (int x=0 ;  x < w ; x++)
        {
            double fx = x * (maxX-minX) / w + minX;
            //ROS_INFO("Drawing at (%d,%d) (%d, %d)", ix, iy, x, y);
            double p = get(fx, fy);
            if (p >= 0)
                putPixel(surf, x, y, SDL_MapRGB(surf->format, 255, 255*(1-p), 255*(1-p)), false);
            else
                putPixel(surf, x, y, SDL_MapRGB(surf->format, 200, 200, 255), false);
        }
    }
    SDL_UnlockSurface(surf);
    
    //ROS_INFO("Grid drawn");
    return surf;
}

/**
 * @brief Maps an angle to fit in the range [0 ; 2*M_PI].
 */
double DeadReckoning::modAngle(double rad)
{
    return fmod(fmod(rad, 2*M_PI) + 2*M_PI, 2*M_PI);
}

/**
 * @brief Converts a points cloud message into a laser scan message, possibly loosing information.
 *
 * This function was adapted from https://github.com/ros-perception/pointcloud_to_laserscan/blob/indigo-devel/src/pointcloud_to_laserscan_nodelet.cpp
 *
 * @param cloud_msg The points cloud message to convert.
 * @param output A reference to the laser scan message to fill.
 */
void DeadReckoning::pointCloudToLaserScan(const sensor_msgs::PointCloud2ConstPtr &cloud_msg, sensor_msgs::LaserScan& output)
{
    output.angle_min = -30.0 * M_PI/180;
    output.angle_max = 30.0 * M_PI/180;
    output.angle_increment = ANGLE_PRECISION * M_PI / 180;
    output.time_increment = 0.0;
    output.scan_time = 1.0 / 30.0;
    output.range_min = 0.45;
    output.range_max = 15.0;

    //determine amount of rays to create
    uint32_t ranges_size = std::ceil((output.angle_max - output.angle_min) / output.angle_increment);
    output.ranges.assign(ranges_size, std::numeric_limits<double>::infinity());

    // Iterate through pointcloud
    for (sensor_msgs::PointCloud2ConstIterator<float>
            iter_x(*cloud_msg, "x"), iter_y(*cloud_msg, "y"), iter_z(*cloud_msg, "z");
            iter_x != iter_x.end();
            ++iter_x, ++iter_y, ++iter_z)
    {

        if (std::isnan(*iter_x) || std::isnan(*iter_y) || std::isnan(*iter_z))
            continue;

        if (*iter_y > 0.5 || *iter_y < -0.5)
            continue;
        
        double range = hypot(*iter_x, *iter_z);
        if (range < output.range_min || range > output.range_max)
            continue;

        //ROS_INFO("Range at (%.3f, %.3f): %.3f", *iter_x, *iter_y, range);
        
        double angle = -atan2(*iter_x, *iter_z);
        if (angle < output.angle_min || angle > output.angle_max)
            continue;
        
        //ROS_INFO("Range at angle %.3f: %.3f", angle*180/M_PI, range);

        //overwrite range at laserscan ray if new range is smaller
        int index = (angle - output.angle_min) / output.angle_increment;
        if (range < output.ranges[index])
        {
            output.ranges[index] = range;
        }
    }
}

/**
 * @brief Wrapper to load an image into a SDL Surface.
 *
 * @param path The path to the image to load.
 * @return The newly allocated surface containing the image (deletion is caller's job - see SDL_FreeSurface()).
 */
SDL_Surface* DeadReckoning::loadImg(std::string path)
{
    SDL_Surface *surf = IMG_Load(path.c_str());
    if (!surf)
        ROS_ERROR("Unable to load image \"%s\": %s", path.c_str(), IMG_GetError());
    return surf;
}

/**
 * @brief Gets the estimated position of the robot at a given time, according to history records.
 *
 * @param time The time at which the robot position is to be estimated.
 * @return The estimated position, with a time stamp.
 */
DeadReckoning::StampedPos DeadReckoning::getPosForTime(const ros::Time& time)
{
    if (m_simulation)
        return m_position;
    
    StampedPos prevPos = m_positionsHist[m_positionsHistIdx];
    if (isnan(prevPos.x) || isnan(prevPos.y) || isnan(prevPos.z) || prevPos.t >= time)
        return prevPos;
    
    for (int i=1 ; i < SIZE_POSITIONS_HIST ; i++)
    {
        StampedPos pos = m_positionsHist[(m_positionsHistIdx+i) % SIZE_POSITIONS_HIST];
        if (!isnan(pos.x) && !isnan(pos.y) && !isnan(pos.z) && pos.t >= time)
            return time-prevPos.t >= pos.t-time ? pos : prevPos;
        prevPos = pos;
    }
    
    return prevPos;
}

/**
 * @brief Callback of the friends detection topic.
 *
 * Converts the information received about the friends in real world positions and stores them internally.
 *
 * @param friendsInfos The received FriendsInfos message.
 */
void DeadReckoning::friendsCallback(const detect_friend::FriendsInfos::ConstPtr& friendsInfos)
{
    for (int i=0 ; i < NB_FRIENDS ; i++)
        m_friendInSight[i] = false;
    for (std::vector<detect_friend::Friend_id>::const_iterator it = friendsInfos->infos.begin() ; it != friendsInfos->infos.end() ; it++)
    {
        if (it->id < 0 || it->id >= NB_FRIENDS)
            continue;
        double angle = -atan(it->dx / it->dz);
        double d = hypot(it->dx, it->dz);
        
        StampedPos pos = getPosForTime(it->Time);
        angle += pos.z;
        m_friendsPos[it->id].x = d * cos(angle) + pos.x;
        m_friendsPos[it->id].y = d * sin(angle) + pos.y;
        m_friendsPos[it->id].t = it->Time;
        m_friendInSight[it->id] = true;
    }
}

/**
 * @brief Callback of the markers detection topic.
 *
 * Converts the information received about the markers in real world positions and stores them internally.
 *
 * @param markersInfos The received MarkersInfos message.
 */
void DeadReckoning::markersCallback(const detect_marker::MarkersInfos::ConstPtr& markersInfos)
{
    for (int i=0 ; i < 256 ; i++)
        m_markerInSight[i] = false;
    for (std::vector<detect_marker::MarkerInfo>::const_iterator it = markersInfos->infos.begin() ; it != markersInfos->infos.end() ; it++)
    {
        if (it->id < 0 || it->id >= 256)
            continue;
        double angle = -atan(it->dx / it->dz);
        double d = hypot(it->dx, it->dz);
        
        StampedPos pos = getPosForTime(markersInfos->time);
        angle += pos.z;
        m_markersPos[it->id].x = d * cos(angle) + pos.x;
        m_markersPos[it->id].y = d * sin(angle) + pos.y;
        m_markersPos[it->id].t = markersInfos->time;
        m_markerInSight[it->id] = true;
    }
}

/**
 * @brief Callback of the inertial sensors topic.
 *
 * In real life, stores the estimated orientation of the robot to compute the estimated position later (when receiving an Odometry message).
 *
 * @param imu The received Imu message.
 */
void DeadReckoning::IMUCallback(const sensor_msgs::Imu::ConstPtr& imu)
{
    if (!m_simulation)
    {
        double angle = 2*asin(imu->orientation.z);
        if (isnan(m_offsetZ))
            m_offsetZ = m_position.z - angle;
        m_position.z = modAngle(angle + m_offsetZ);
    }
}

/**
 * @brief Callback of the odometry topic.
 *
 * In real life, computes the new estimated robot position according to the odometry information received (wheel sensors) and current orientation (received through IMU sensors).
 *
 * @param odom The received Odometry message.
 */
void DeadReckoning::odomCallback(const nav_msgs::Odometry::ConstPtr& odom)
{
    if (!m_simulation)
    {
        double angle = 2*asin(odom->pose.pose.orientation.z);
        if (isnan(m_offsetX) || isnan(m_offsetY) || isnan(m_offsetZOdom))
        {
            m_offsetX = m_position.x - odom->pose.pose.position.x;
            m_offsetY = m_position.y - odom->pose.pose.position.y;
            m_offsetZOdom = m_position.z - angle;
        }
        m_position.x = odom->pose.pose.position.x * cos(m_offsetZOdom) - odom->pose.pose.position.y * sin(m_offsetZOdom) + m_offsetX;
        m_position.y = odom->pose.pose.position.x * sin(m_offsetZOdom) + odom->pose.pose.position.y * cos(m_offsetZOdom) + m_offsetY;
        m_position.t = odom->header.stamp;
        
        m_positionsHist[m_positionsHistIdx] = m_position;
        m_positionsHistIdx = (m_positionsHistIdx+1) % SIZE_POSITIONS_HIST;
    }
}

/**
 * @brief Callback of the velocity orders topic.
 *
 * In simulation, computes the new estimated robot position according to the orders sent to it.
 *
 * @param order The received Twist message.
 */
void DeadReckoning::moveOrderCallback(const geometry_msgs::Twist::ConstPtr& order)
{
    //ROS_INFO("Received order: v=%.3f, r=%.3f", order->linear.x, order->angular.z);
    if (m_simulation)
    {
        ros::Time t = ros::Time::now();
        double deltaTime = (t - m_position.t).toSec();
        m_position.t = t;
        
        if (fabs(m_angularSpeed) > 1e-5)
        {
            double r = m_linearSpeed / m_angularSpeed;
            double deltaAngle = m_angularSpeed * deltaTime;
            m_position.x += r * (sin(deltaAngle + m_position.z) - sin(m_position.z));
            m_position.y -= r * (cos(deltaAngle + m_position.z) - cos(m_position.z));
            m_position.z += deltaAngle;
        }
        else
        {
            m_position.x += m_linearSpeed * deltaTime * cos(m_position.z);
            m_position.y += m_linearSpeed * deltaTime * sin(m_position.z);
        }
    }

    m_linearSpeed = order->linear.x;
    m_angularSpeed = order->angular.z;
}

/**
 * @brief Callback of the topic of the local map node associated to the laser scan.
 *
 * Updates the internal Grid with the data of the new occupancy grid and publishes the result under the name "dead_reckoning/scan_grid" (see DeadReckoning::publishGrid()).
 *
 * @param occ The received OccupancyGrid message.
 */
void DeadReckoning::localMapScanCallback(const nav_msgs::OccupancyGrid::ConstPtr& occ)
{
    //ROS_INFO("Received local map");
    updateGridFromOccupancy(occ, m_scanGrid);
    publishGrid(m_scanGrid, m_scanGridPub);
}

/**
 * @brief Callback of the topic of the local map node associated to the depth image.
 *
 * Updates the internal Grid with the data of the new occupancy grid and publishes the result under the name "dead_reckoning/depth_grid" (see DeadReckoning::publishGrid()).
 *
 * @param occ The received OccupancyGrid message.
 */
void DeadReckoning::localMapDepthCallback(const nav_msgs::OccupancyGrid::ConstPtr& occ)
{
    //ROS_INFO("Received local map");
    updateGridFromOccupancy(occ, m_depthGrid);
    publishGrid(m_depthGrid, m_depthGridPub);
}

/**
 * @brief Updates the Grid according to the content of an OccupancyGrid message (received from local map nodes).
 *
 * @param occ The received OccupancyGrid message.
 * @param grid A reference to the grid to update.
 */
void DeadReckoning::updateGridFromOccupancy(const nav_msgs::OccupancyGrid::ConstPtr& occ, Grid& grid)
{
    ros::Time t = ros::Time::now();
    for (int y=0 ; y < occ->info.height ; y++)
    {
        int k = y * occ->info.width;
        double fy = m_position.y + (y-(int)occ->info.height/2)*occ->info.resolution;
        for (int x=0 ; x < occ->info.width ; x++)
        {
            double p = occ->data[k+x] / 100.0;
            if (p >= 0)
            {
                //ROS_INFO("Point at (%d, %d): %.3f", x, y, p);
                double fx = m_position.x + (x-(int)occ->info.width/2)*occ->info.resolution;
                grid.addPoint(fx, fy, t, p);
            }
        }
    }
}

/**
 * @brief Updates the points cloud and other data according to the content of an LaserScan message (received from laser scan nodes or converted from depth images nodes).
 *
 * @param scan The received LaserScan message.
 * @param invert True if the laser scan should be inverted (180° rotation) before processing, the original message is left untouched in any case.
 * @param ranges An array to store the 360° ranges extracted from laser scan data, should be wide enough (use m_scanRanges or m_depthRanges).
 * @param cloudPoints An array to store the points cloud extracted from laser scan data, should contains NB_CLOUDPOINTS points (use m_scanCloudPoints or m_depthCloudPoints).
 * @param startIdx The index of the oldest point in the cloudPoints array (will be the first to be removed, then points are replaced in increasing index order, modulo the size of the array).
 */
void DeadReckoning::processLaserScan(sensor_msgs::LaserScan& scan, bool invert, double *ranges, Vector *cloudPoints, int& startIdx)
{
    int maxIdx = ceil(360 / ANGLE_PRECISION);
    int nbRanges = ceil((scan.angle_max - scan.angle_min) / scan.angle_increment);
    int prevAngleIdx = -1;
    ros::Time t = ros::Time::now();
    
    for (int i=0 ; i < nbRanges ; i++)
    {
        if (scan.ranges[i] > MAX_RANGE || scan.ranges[i] >= scan.range_max)
            scan.ranges[i] = std::numeric_limits<float>::infinity();
        double range = scan.ranges[i];
        
        double angle = modAngle(scan.angle_min + i * scan.angle_increment);
        if (invert)
            angle = modAngle(angle + M_PI); //The laser scan points towards robot's back
        
        int angleIdx = round(angle * 180 / (M_PI * ANGLE_PRECISION));
        if (angleIdx >= maxIdx)
            angleIdx = 0;
        if (angleIdx != prevAngleIdx)
        {
            prevAngleIdx = angleIdx;
            ranges[angleIdx] = range;
        }
        else if (ranges[angleIdx] > range)
            ranges[angleIdx] = range;
        
        if (!std::isinf(range))
        {
            double endX = range * cos(angle + m_position.z) + m_position.x;
            double endY = range * sin(angle + m_position.z) + m_position.y;
            int cloudPointIdx = (i+startIdx) % NB_CLOUDPOINTS;
            cloudPoints[cloudPointIdx].x = endX;
            cloudPoints[cloudPointIdx].y = endY;
            //ROS_INFO("Added cloud point (%.3f, %.3f)", endX, endY);
        }
    }
    startIdx += nbRanges;
}

/**
 * @brief Callback of the laser scan topic.
 *
 * Compute new cloud points and new proximity ranges from the laser scan, which are stored in this instance.
 * Publishes the resulting laser scan under the name "/local_map_scan/scan" to be used by local map nodes.
 *
 * @param scan The received LaserScan message.
 */
void DeadReckoning::scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan)
{
    sensor_msgs::LaserScan scanCopy = *scan;
    processLaserScan(scanCopy, !m_simulation, m_scanRanges, m_scanCloudPoints, m_scanCloudPointsStartIdx);
    
    scanCopy.header.frame_id = LOCALMAP_SCAN_TRANSFORM_NAME;
    m_laserScanPub.publish(scanCopy);
}

/**
 * @brief Callback of the depth image topic.
 *
 * Converts the depth image into a laser scan and use it to compute new cloud points and new proximity ranges which are stored in this instance.
 * Publishes the resulting laser scan under the name "/local_map_depth/scan" to be used by local map nodes.
 *
 * @param cloud The received PointCloud2 message.
 */
void DeadReckoning::depthCallback(const sensor_msgs::PointCloud2::ConstPtr& cloud)
{
    sensor_msgs::LaserScan scan;
    pointCloudToLaserScan(cloud, scan);
    processLaserScan(scan, false, m_depthRanges, m_depthCloudPoints, m_depthCloudPointsStartIdx);
    
    scan.header.frame_id = LOCALMAP_DEPTH_TRANSFORM_NAME;
    m_laserDepthPub.publish(scan);
}

/**
 * @brief Publishes all transforms related to the robot / Grid positions and orientations needed by other nodes (movement and local maps).
 */
void DeadReckoning::publishTransforms()
{
    tf::Transform transform;
    tf::Quaternion q;
    
    transform.setOrigin( tf::Vector3(m_position.x, m_position.y, 0.0) );
    q.setRPY(0, 0, m_simulation ? m_position.z : modAngle(m_position.z+M_PI));
    transform.setRotation(q);
    m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", LOCALMAP_SCAN_TRANSFORM_NAME));
    
    if (!m_simulation)
    {
        transform.setOrigin( tf::Vector3(m_position.x, m_position.y, 0.0) );
        q.setRPY(0, 0, m_position.z);
        transform.setRotation(q);
        m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", LOCALMAP_DEPTH_TRANSFORM_NAME));
    }
    
    transform.setOrigin( tf::Vector3(m_position.x, m_position.y, 0.0) );
    q.setRPY(0, 0, m_position.z);
    transform.setRotation(q);
    m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", ROBOTPOS_TRANSFORM_NAME));
    
    transform.setOrigin( tf::Vector3(m_scanGrid.minX(), m_scanGrid.minY(), 0.0) );
    q.setRPY(0, 0, 0);
    transform.setRotation(q);
    m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", SCANGRIDPOS_TRANSFORM_NAME));
    
    if (!m_simulation)
    {
        transform.setOrigin( tf::Vector3(m_depthGrid.minX(), m_depthGrid.minY(), 0.0) );
        q.setRPY(0, 0, 0);
        transform.setRotation(q);
        m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", DEPTHGRIDPOS_TRANSFORM_NAME));
    }
}

/**
 * @brief Publishes all known positions of the markers via transforms.
 */
void DeadReckoning::publishMarkersTransforms()
{
    tf::Transform transform;
    tf::Quaternion q;
    char transformName[100];
    
    q.setRPY(0, 0, 0);
    transform.setRotation(q);
        
    for (int i=0 ; i < 256 ; i++)
    {
        if (isnan(m_markersPos[i].x) || isnan(m_markersPos[i].y))
            continue;
        snprintf(transformName, 100, "%s_%d", MARKERPOS_TRANSFORM_NAME.c_str(), i);
        transform.setOrigin( tf::Vector3(m_markersPos[i].x, m_markersPos[i].y, 0.0) );
        m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", transformName));
    }
}

/**
 * @brief Publishes all known positions of the friends via transforms.
 */
void DeadReckoning::publishFriendsTransforms()
{
    tf::Transform transform;
    tf::Quaternion q;
    char transformName[100];
    
    q.setRPY(0, 0, 0);
    transform.setRotation(q);
        
    for (int i=0 ; i < NB_FRIENDS ; i++)
    {
        if (isnan(m_friendsPos[i].x) || isnan(m_friendsPos[i].y))
            continue;
        snprintf(transformName, 100, "%s_%d", FRIENDPOS_TRANSFORM_NAME.c_str(), i);
        transform.setOrigin( tf::Vector3(m_friendsPos[i].x, m_friendsPos[i].y, 0.0) );
        m_transformBroadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", transformName));
    }
}

/**
 * @brief Publishes a Grid through a given pusblisher.
 *
 * The grid is published under the form of a dead_reckoning::Grid message, basically a 1D array containing obstacle probabilities stored in column-major order.
 * See Grid::getAll() for more information.
 *
 * @param grid The Grid to publish.
 * @param pub The publisher to use to publish the Grid.
 */
void DeadReckoning::publishGrid(const Grid& grid, ros::Publisher& pub)
{
    dead_reckoning::Grid gridMsg;
    int width, height;
    double scale;
    double *data = grid.getAll(&width, &height, &scale);
    if (data == NULL)
        return;
    gridMsg.data.assign(data, data+width*height);
    gridMsg.width = width;
    gridMsg.height = height;
    gridMsg.scale = scale;
    gridMsg.x = grid.minX();
    gridMsg.y = grid.minY();
    pub.publish(gridMsg);
    delete data;
}

/**
 * @brief Initializes the SDL, creates the display and allocates needed surfaces.
 *
 * @return True on success.
 */
bool DeadReckoning::initSDL()
{
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
    {
        ROS_ERROR("Unable to initialize the SDL.");
        return false;
    }
    
    int flags = IMG_INIT_PNG;
    if((IMG_Init(flags) & flags) != flags)
    {
        ROS_ERROR("Unable to initialize IMG: %s", IMG_GetError());
        return false;
    }

    m_screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_SWSURFACE);
    if (!m_screen)
    {
        ROS_ERROR("Unable to create display window.");
        return false;
    }
    SDL_Flip(m_screen);
    
    std::string packagePath = "~";
    if (!m_node.getParam("package_path", packagePath))
       ROS_WARN("The package path is not set, it will default to '~'.");

    if ((m_robotSurf = loadImg(packagePath + "/turtlebot_small.png")) == NULL)
        return false;
    if ((m_markerSurf = loadImg(packagePath + "/target_small.png")) == NULL)
        return false;
    if ((m_markerSurfTransparent = loadImg(packagePath + "/target_small_tr.png")) == NULL)
        return false;
    
    m_friendSurf = new SDL_Surface*[NB_FRIENDS];
    m_friendSurfTransparent = new SDL_Surface*[NB_FRIENDS];
    //TODO check if not NULL
    
    if ((m_friendSurf[0] = loadImg(packagePath + "/star_small.png")) == NULL)
        return false;
    if ((m_friendSurf[2] = loadImg(packagePath + "/coin_small.png")) == NULL)
        return false;
    if ((m_friendSurf[1] = loadImg(packagePath + "/mushroom_small.png")) == NULL)
        return false;
    if ((m_friendSurfTransparent[0] = loadImg(packagePath + "/star_small_tr.png")) == NULL)
        return false;
    if ((m_friendSurfTransparent[2] = loadImg(packagePath + "/coin_small_tr.png")) == NULL)
        return false;
    if ((m_friendSurfTransparent[1] = loadImg(packagePath + "/mushroom_small_tr.png")) == NULL)
        return false;
    
    m_gridSurf = SDL_CreateRGBSurface(SDL_HWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 32,0,0,0,0);
    if (!m_gridSurf)
    {
        ROS_ERROR("Unable to create the grid surface.");
        return false;
    }
    SDL_SetColorKey(m_gridSurf, SDL_SRCCOLORKEY, SDL_MapRGB(m_gridSurf->format, 0,0,0));
    
    return true;
}

/**
 * @brief Updates the display according to current internal data.
 */
void DeadReckoning::updateDisplay()
{
    //ROS_INFO("Updating display");
    
    Vector pos = {m_position.x, m_position.y};
    Vector speed = {m_linearSpeed * cos(m_position.z), m_linearSpeed * sin(m_position.z)};
    Vector acceleration = {-m_linearSpeed * m_angularSpeed * sin(m_position.z), m_linearSpeed * m_angularSpeed * cos(m_position.z)};
    
    SDL_Rect rect = {0};
    SDL_FillRect(m_screen, 0, SDL_MapRGB(m_screen->format, 255,255,255));
    
    SDL_FillRect(m_gridSurf, NULL, SDL_MapRGB(m_gridSurf->format, 0,0,0));
    m_scanGrid.draw(SCREEN_WIDTH, SCREEN_HEIGHT, m_minX, m_maxX, m_minY, m_maxY, m_gridSurf);
    SDL_BlitSurface(m_gridSurf, NULL, m_screen, &rect);
    
    /*if (!m_simulation)
    {
        SDL_FillRect(m_gridSurf, NULL, SDL_MapRGB(m_gridSurf->format, 0,0,0));
        m_depthGrid.draw(SCREEN_WIDTH, SCREEN_HEIGHT, m_minX, m_maxX, m_minY, m_maxY, m_gridSurf);
        SDL_BlitSurface(m_gridSurf, NULL, m_screen, &rect);
    }*/
    
    double kx = SCREEN_WIDTH / (m_maxX - m_minX);
    double ky = SCREEN_HEIGHT / (m_maxY - m_minY);
    
    Uint32 green = SDL_MapRGB(m_screen->format, 0,128,0);
    Uint32 blue = SDL_MapRGB(m_screen->format, 0,0,255);
    for (int i=0 ; i < NB_CLOUDPOINTS ; i++)
    {
        if (!isnan(m_scanCloudPoints[i].x) && !isnan(m_scanCloudPoints[i].y))
            putPixel(m_screen, (m_scanCloudPoints[i].x-m_minX) * kx, SCREEN_HEIGHT - (m_scanCloudPoints[i].y-m_minY) * ky, green);
        if (!m_simulation && !isnan(m_depthCloudPoints[i].x) && !isnan(m_depthCloudPoints[i].y))
            putPixel(m_screen, (m_depthCloudPoints[i].x-m_minX) * kx, SCREEN_HEIGHT - (m_depthCloudPoints[i].y-m_minY) * ky, blue);
    }

    int x, y;
    for (int i=0 ; i < 256 ; i++)
    {
        if (isnan(m_markersPos[i].x) || isnan(m_markersPos[i].y))
            continue;
        convertPosToDisplayCoord(m_markersPos[i].x, m_markersPos[i].y, x, y);
        rect.x = x-m_markerSurf->w/2;
        rect.y = y-m_markerSurf->h/2;
        SDL_BlitSurface(m_markerInSight[i] ? m_markerSurf : m_markerSurfTransparent, NULL, m_screen, &rect);
    }
    
    for (int i=0 ; i < NB_FRIENDS ; i++)
    {
        if (isnan(m_friendsPos[i].x) || isnan(m_friendsPos[i].y))
            continue;
        convertPosToDisplayCoord(m_friendsPos[i].x, m_friendsPos[i].y, x, y);
        rect.x = x-m_friendSurf[i]->w/2;
        rect.y = y-m_friendSurf[i]->h/2;
        SDL_BlitSurface(m_friendInSight[i] ? m_friendSurf[i] : m_friendSurfTransparent[i], NULL, m_screen, &rect);
    }
    
    convertPosToDisplayCoord(m_position.x, m_position.y, x, y);
    
    SDL_Surface *robotSurf = rotozoomSurface(m_robotSurf, m_position.z*180/M_PI, 1.0, 1);
    rect.x = x-robotSurf->w/2;
    rect.y = y-robotSurf->h/2;
    SDL_BlitSurface(robotSurf, NULL, m_screen, &rect);
    SDL_FreeSurface(robotSurf);
    
    drawLine(m_screen, x, y, x + 3*speed.x*kx, y - 3*speed.y*ky, createColor(0,0,255));
    drawLine(m_screen, x, y, x + 3*acceleration.x*kx, y - 3*acceleration.y*ky, createColor(255,0,0));
    drawLine(m_screen, x, y, x+30*cos(m_position.z), y-30*sin(m_position.z), createColor(0,0,0));
    
    SDL_Flip(m_screen);
    
    //ROS_INFO("Display updated.");
}

/**
 * @brief Converts a real world position into display coordinates.
 *
 * @param fx The x-coordinate of the real world position.
 * @param fy The y-coordinate of the real world position.
 * @param x A reference to the variable in which the x display coordinate will be stored.
 * @param y A reference to the variable in which the y display coordinate will be stored.
 */
void DeadReckoning::convertPosToDisplayCoord(double fx, double fy, int& x, int& y)
{
    double kx = SCREEN_WIDTH / (m_maxX - m_minX);
    double ky = SCREEN_HEIGHT / (m_maxY - m_minY);
    x = (fx-m_minX) * kx;
    y = SCREEN_HEIGHT - (fy-m_minY) * ky;
}

/**
 * @brief Standard constructor.
 *
 * Creates and initializes a fresh empty instance of a Dead Reckoning monitor with specified settings.
 *
 * @param node The main node handle.
 * @param simulation True to use simulation behavior (no odometry, no IMU, no depth image, ...).
 * @param minX The x-coordinate of the point mapped to the upper-left corner of the display in the real world.
 * @param maxX The x-coordinate of the point mapped to the lower-right corner of the display in the real world.
 * @param minY The y-coordinate of the point mapped to the upper-left corner of the display in the real world.
 * @param maxY The y-coordinate of the point mapped to the lower-right corner of the display in the real world.
 */
DeadReckoning::DeadReckoning(ros::NodeHandle& node, bool simulation, double minX, double maxX, double minY, double maxY):
    m_node(node), m_simulation(simulation), m_ok(false),
    m_scanCloudPointsStartIdx(0), m_depthCloudPointsStartIdx(0),
    m_angularSpeed(0), m_linearSpeed(0),
    m_minX(minX), m_maxX(maxX), m_minY(minY), m_maxY(maxY)
{
    if (!initSDL())
        return;
    
    if (m_simulation)
    {
        m_position.x = 2.0;
        m_position.y = 2.0;
        m_position.z = 0.0;
    }
    else
    {
        m_position.x = 0.0;
        m_position.y = 0.0;
        m_position.z = M_PI/2;
    }
    
    m_offsetX = nan("");
    m_offsetY = nan("");
    m_offsetZ = nan("");
    m_offsetZOdom = nan("");
    
    m_positionsHist = new StampedPos[SIZE_POSITIONS_HIST];
    checkPointerOk(m_positionsHist, "Unable to allocate positions buffer.");
    for (int i=0 ; i < SIZE_POSITIONS_HIST ; i++)
    {
        m_positionsHist[i].x = nan("");
        m_positionsHist[i].y = nan("");
        m_positionsHist[i].z = nan("");
    }
    m_positionsHistIdx = 0;
    
    for (int i=0 ; i < 256 ; i++)
    {
        m_markersPos[i].x = nan("");
        m_markersPos[i].y = nan("");
        m_markerInSight[i] = false;
    }
    
    m_friendsPos = new StampedPos[NB_FRIENDS];
    checkPointerOk(m_friendsPos, "Unable to allocate friends positions buffer.");
    m_friendInSight = new bool[NB_FRIENDS];
    checkPointerOk(m_friendInSight, "Unable to allocate friends status buffer.");
    for (int i=0 ; i < NB_FRIENDS ; i++)
    {
        m_friendsPos[i].x = nan("");
        m_friendsPos[i].y = nan("");
        m_friendInSight[i] = false;
    }
    
    m_scanGrid = Grid(0.05, ros::Duration(120.0), m_minX, m_maxX, m_minY, m_maxY, false);
    m_depthGrid = m_scanGrid;
    
    int nbRanges = ceil(360 / ANGLE_PRECISION);
    m_scanRanges = new double[nbRanges];
    checkPointerOk(m_scanRanges, "Unable to allocate scan ranges.");
    for (int i=0 ; i < nbRanges ; i++)
        m_scanRanges[i] = nan("");
    
    m_depthRanges = new double[nbRanges];
    checkPointerOk(m_depthRanges, "Unable to allocate depth ranges.");
    memcpy(m_depthRanges, m_scanRanges, sizeof(double)*nbRanges);

    m_scanCloudPoints = new Vector[NB_CLOUDPOINTS];
    checkPointerOk(m_scanCloudPoints, "Unable to allocate scan cloud points.");
    for (int i=0 ; i < NB_CLOUDPOINTS ; i++)
    {
        m_scanCloudPoints[i].x = nan("");
        m_scanCloudPoints[i].y = nan("");
    }
    
    m_depthCloudPoints = new Vector[NB_CLOUDPOINTS];
    checkPointerOk(m_depthCloudPoints, "Unable to allocate depth cloud points.");
    memcpy(m_depthCloudPoints, m_scanCloudPoints, sizeof(Vector)*NB_CLOUDPOINTS);
    
    // Subscribe to the robot's laser scan topic
    m_laserSub = m_node.subscribe<sensor_msgs::LaserScan>("/scan", 1, &DeadReckoning::scanCallback, this);
    ROS_INFO("Waiting for laser scan...");
    ros::Rate rate(10);
    while (ros::ok() && m_laserSub.getNumPublishers() <= 0)
        rate.sleep();
    checkRosOk_v();
    
    // Subscribe to the robot's depth cloud topic
    m_depthSub = m_node.subscribe<sensor_msgs::PointCloud2>("/camera/depth/points", 1, &DeadReckoning::depthCallback, this);
    if (!m_simulation)
    {
        ROS_INFO("Waiting for depth cloud...");
        while (ros::ok() && m_depthSub.getNumPublishers() <= 0)
            rate.sleep();
        checkRosOk_v();
    }
    
    m_orderSub = m_node.subscribe<geometry_msgs::Twist>("/mobile_base/commands/velocity", 1000, &DeadReckoning::moveOrderCallback, this);
    if (m_simulation)
    {
        ROS_INFO("Waiting for commands publisher...");
        while (ros::ok() && m_orderSub.getNumPublishers() <= 0)
            rate.sleep();
        checkRosOk_v();
    }
    
    m_odomSub = m_node.subscribe<nav_msgs::Odometry>("/odom", 1000, &DeadReckoning::odomCallback, this);
    if (!m_simulation)
    {
        ROS_INFO("Waiting for odometry...");
        while (ros::ok() && m_odomSub.getNumPublishers() <= 0)
            rate.sleep();
        checkRosOk_v();
    }
    
    m_imuSub = m_node.subscribe<sensor_msgs::Imu>("/mobile_base/sensors/imu_data", 1000, &DeadReckoning::IMUCallback, this);
    if (!m_simulation)
    {
        ROS_INFO("Waiting for IMU...");
        while (ros::ok() && m_imuSub.getNumPublishers() <= 0)
            rate.sleep();
        checkRosOk_v();
    }
    
    m_laserScanPub = m_node.advertise<sensor_msgs::LaserScan>("/local_map_scan/scan", 10);
    m_localMapScanSub = m_node.subscribe<nav_msgs::OccupancyGrid>("/local_map_scan/local_map", 10, &DeadReckoning::localMapScanCallback, this);
    ROS_INFO("Waiting for scan local map...");
    while (ros::ok() && (m_localMapScanSub.getNumPublishers() <= 0 || m_laserScanPub.getNumSubscribers() <= 0))
        rate.sleep();
    checkRosOk_v();
    
    m_laserDepthPub = m_node.advertise<sensor_msgs::LaserScan>("/local_map_depth/scan", 10);
    m_localMapDepthSub = m_node.subscribe<nav_msgs::OccupancyGrid>("/local_map_depth/local_map", 10, &DeadReckoning::localMapDepthCallback, this);
    if (!m_simulation)
    {
        ROS_INFO("Waiting for depth local map...");
        while (ros::ok() && (m_localMapDepthSub.getNumPublishers() <= 0 || m_laserDepthPub.getNumSubscribers() <= 0))
            rate.sleep();
        checkRosOk_v();
    }
    
    m_markersSub = m_node.subscribe<detect_marker::MarkersInfos>("/markerinfo", 10, &DeadReckoning::markersCallback, this);
    ROS_INFO("Waiting for marker infos...");
    while (ros::ok() && m_markersSub.getNumPublishers() <= 0)
        rate.sleep();
    checkRosOk_v();
    
    m_friendsSub = m_node.subscribe<detect_friend::FriendsInfos>("/friendinfo", 10, &DeadReckoning::friendsCallback, this);
    ROS_INFO("Waiting for friends infos...");
    while (ros::ok() && m_friendsSub.getNumPublishers() <= 0)
        rate.sleep();
    checkRosOk_v();
    
    ROS_INFO("Creating grids publishers...");
    m_scanGridPub = m_node.advertise<dead_reckoning::Grid>("/dead_reckoning/scan_grid", 10);
    m_depthGridPub = m_node.advertise<dead_reckoning::Grid>("/dead_reckoning/depth_grid", 10);
    
    m_ok = true;
    ROS_INFO("Ok, let's go.");
}

/**
 * @brief Destructor.
 *
 * Frees internal storage.
 */
DeadReckoning::~DeadReckoning()
{
    if (m_positionsHist != NULL)
        delete m_positionsHist;
    if (m_scanRanges != NULL)
        delete m_scanRanges;
    if (m_scanCloudPoints != NULL)
        delete m_scanCloudPoints;
    if (m_depthRanges != NULL)
        delete m_depthRanges;
    if (m_depthCloudPoints != NULL)
        delete m_depthCloudPoints;
    if (m_gridSurf != NULL)
        SDL_FreeSurface(m_gridSurf);
    if (m_markerSurf != NULL)
        SDL_FreeSurface(m_markerSurf);
    if (m_markerSurfTransparent != NULL)
        SDL_FreeSurface(m_markerSurfTransparent);
    if (m_friendsPos != NULL)
        delete m_friendsPos;
    if (m_friendInSight != NULL)
        delete m_friendInSight;
    if (m_friendSurf != NULL)
    {
        for (int i=0 ; i < NB_FRIENDS ; i++)
        {
            if (m_friendSurf[i] != NULL)
                SDL_FreeSurface(m_friendSurf[i]);
        }
        delete m_friendSurf;
    }
    if (m_friendSurfTransparent != NULL)
    {
        for (int i=0 ; i < NB_FRIENDS ; i++)
        {
            if (m_friendSurfTransparent[i] != NULL)
                SDL_FreeSurface(m_friendSurfTransparent[i]);
        }
        delete m_friendSurfTransparent;
    }
    IMG_Quit();
    SDL_Quit();
}

/**
 * @brief Starts processing.
 *
 * This function does not return until a SIGTERM is received or ROS stops working.
 */
void DeadReckoning::reckon()
{
    ROS_INFO("Starting reckoning.");
    ros::Rate rate(10);
    while (ros::ok())
    {
        ros::spinOnce();
        publishTransforms();
        publishMarkersTransforms();
        publishFriendsTransforms();
        updateDisplay();
        rate.sleep();
    }
}

/**
 * @brief Tells if the instance is ready to start.
 *
 * @return True if it ready.
 */
bool DeadReckoning::ready()
{
    return m_ok;
}

const double DeadReckoning::ANGLE_PRECISION = 0.1;                                              /*!< The angle delta between two consecutive proximity ranges, in degrees. */
const int DeadReckoning::NB_CLOUDPOINTS = 1000;                                                 /*!< The size of the internal cloud points buffer. */
const double DeadReckoning::MAX_RANGE = 15.0;                                                   /*!< The maximum range to keep for proximity ranges, greater ranges will be stored as infinity. */
const int DeadReckoning::SCREEN_HEIGHT = 600;                                                   /*!< The height of the display in pixels. */
const int DeadReckoning::SCREEN_WIDTH = 600;                                                    /*!< The width of the display in pixels. */
const std::string DeadReckoning::LOCALMAP_SCAN_TRANSFORM_NAME = "localmap_pos_scan";            /*!< The name of the topic on which laser scans formatted for local map nodes are published. */
const std::string DeadReckoning::LOCALMAP_DEPTH_TRANSFORM_NAME = "localmap_pos_depth";          /*!< The name of the topic on which depth images formatted for local map nodes are published. */
const std::string DeadReckoning::ROBOTPOS_TRANSFORM_NAME = "deadreckoning_robotpos";            /*!< The name of the transformation through which the estimated robot position is published. */
const std::string DeadReckoning::SCANGRIDPOS_TRANSFORM_NAME = "deadreckoning_scangridpos";      /*!< The name of the transformation through which the position (upper-left corner) of the Grid based on laser scan data is published. */
const std::string DeadReckoning::DEPTHGRIDPOS_TRANSFORM_NAME = "deadreckoning_depthgridpos";    /*!< The name of the transformation through which the position (upper-left corner) of the Grid based on depth image data is published. */
const std::string DeadReckoning::MARKERPOS_TRANSFORM_NAME = "deadreckoning_markerpos";          /*!< The name of the transformation through which the estimated markers positions are published. */
const std::string DeadReckoning::FRIENDPOS_TRANSFORM_NAME = "deadreckoning_friendpos";          /*!< The name of the transformation through which the estimated friends positions are published. */
const int DeadReckoning::SIZE_POSITIONS_HIST = 1000;                                            /*!< The size of the internal positions history. */
const int DeadReckoning::NB_FRIENDS = 3;                                                        /*!< Number of friends currently registered. */
