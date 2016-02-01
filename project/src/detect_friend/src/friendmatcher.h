#include <ros/ros.h>
#include "opencv2/core/core.hpp"

class FriendMatcher
{
    public:
        static cv::Mat computeDistanceImage(const cv::Mat& img, cv::Scalar color);
        
        static const double MARKER_REF_DIST = 480 * 0.2 / 0.175;
        
        struct Rectangle
        {
            cv::Point ul, lr;
        };
        
        struct TemplateInfo
        {
            cv::Mat image;
            Rectangle roi;
            cv::Scalar mainColor;
            double h, w;
            std::string name;
            int id;
        };
        
        struct MatchResult
        {
            double score;
            Rectangle boundingRect;
            cv::Mat colorDiffImg;
        };            
        
        FriendMatcher(const std::vector<TemplateInfo>& templates = std::vector<TemplateInfo>());
        bool addTemplate(const TemplateInfo& templ);
        MatchResult match(const cv::Mat& img, int templateId) const;
        cv::Mat drawResult(const cv::Mat& img, const MatchResult& result) const;
        
    private:
        static const double DILATATION_FACTOR = 25 / 480.0;
        
        static cv::Mat applyLogFilter(const cv::Mat& img);
        static cv::Mat binarizeImageKMeans(const cv::Mat& img);
        static cv::Mat removeIsolatedPixels(const cv::Mat& binImg, int nbMinNeighbours);
        static MatchResult compareBinaryImages(const cv::Mat& binImg1, const cv::Mat& binImg2);
        
        std::vector<TemplateInfo> m_templates;
        
        std::vector<Rectangle> getImageRects(const cv::Mat& img) const;
        MatchResult matchPerspective(const cv::Mat& aFullImg, const std::vector<Rectangle>& rects, const TemplateInfo& templ) const;
};
      