/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright 2012 The MITRE Corporation                                      *
 *                                                                           *
 * Licensed under the Apache License, Version 2.0 (the "License");           *
 * you may not use this file except in compliance with the License.          *
 * You may obtain a copy of the License at                                   *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 * Unless required by applicable law or agreed to in writing, software       *
 * distributed under the License is distributed on an "AS IS" BASIS,         *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
 * See the License for the specific language governing permissions and       *
 * limitations under the License.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <opencv2/imgproc/imgproc.hpp>
#include <openbr_plugin.h>

using namespace cv;
using namespace br;

/*!
 * \ingroup transforms
 * \brief Subdivide matrix into rectangular subregions.
 * \author Josh Klontz \cite jklontz
 */
class RectRegions : public UntrainableTransform
{
    Q_OBJECT
    Q_PROPERTY(int width READ get_width WRITE set_width RESET reset_width STORED false)
    Q_PROPERTY(int height READ get_height WRITE set_height RESET reset_height STORED false)
    Q_PROPERTY(int widthStep READ get_widthStep WRITE set_widthStep RESET reset_widthStep STORED false)
    Q_PROPERTY(int heightStep READ get_heightStep WRITE set_heightStep RESET reset_heightStep STORED false)
    BR_PROPERTY(int, width, 8)
    BR_PROPERTY(int, height, 8)
    BR_PROPERTY(int, widthStep, -1)
    BR_PROPERTY(int, heightStep, -1)

    void project(const Template &src, Template &dst) const
    {
        const int widthStep = this->widthStep == -1 ? width : this->widthStep;
        const int heightStep = this->heightStep == -1 ? height : this->heightStep;
        const Mat &m = src;
        const int xMax = m.cols - width;
        const int yMax = m.rows - height;
        for (int x=0; x <= xMax; x += widthStep)
            for (int y=0; y <= yMax; y += heightStep)
                dst += m(Rect(x, y, width, height));
    }
};

BR_REGISTER(Transform, RectRegions)

/*!
 * \ingroup transforms
 * \brief Turns each row into its own matrix.
 * \author Josh Klontz \cite jklontz
 */
class ByRow : public UntrainableTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        for (int i=0; i<src.m().rows; i++)
            dst += src.m().row(i);
    }
};

BR_REGISTER(Transform, ByRow)

/*!
 * \ingroup transforms
 * \brief Concatenates all input matrices into a single floating point matrix.
 * No requirements are placed on input matrices size and type.
 * \author Josh Klontz \cite jklontz
 */
class Cat : public UntrainableMetaTransform
{
    Q_OBJECT

    void project(const Template &src, Template &dst) const
    {
        int vals = 0;
        foreach (const cv::Mat &m, src)
            vals += m.total() * m.channels();

        Mat cat(1, (int)vals, CV_32FC1);
        int offset = 0;
        foreach (const cv::Mat &m, src) {
            size_t size = m.total() * m.elemSize();
            memcpy(&cat.data[offset], m.ptr(), size);
            offset += size;
        }

        dst.file = src.file;
        dst = cat;
    }
};

BR_REGISTER(Transform, Cat)

/*!
 * \ingroup transforms
 * \brief Duplicates the template data.
 * \author Josh Klontz \cite jklontz
 */
class Dup : public UntrainableMetaTransform
{
    Q_OBJECT
    Q_PROPERTY(int n READ get_n WRITE set_n RESET reset_n STORED false)
    BR_PROPERTY(int, n, 1)

    void project(const Template &src, Template &dst) const
    {
        for (int i=0; i<n; i++)
            dst.merge(src);
    }
};

BR_REGISTER(Transform, Dup)

#include "regions.moc"
