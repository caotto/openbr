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

#ifndef __COMMON_H
#define __COMMON_H

#include <QDebug>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QtAlgorithms>
#include <algorithm>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
#include <assert.h>
#include <math.h>
#include <time.h>

namespace Common
{

/****
Round
    Round floating point to nearest integer.
****/
template <typename T>
int round(T r)
{
    return (r > 0.0) ? floor(r + 0.5) : ceil(r - 0.5);
}


/****
Sort
    Returns a list of pairs sorted by value where:
        pair.first = original value
        pair.second = original index
****/
template <typename T>
QList< QPair<T,int> > Sort(const QList<T> &vals, bool decending = false)
{
    const int size = vals.size();
    QList< QPair<T,int> > pairs; pairs.reserve(size);
    for (int i=0; i<size; i++) pairs.append(QPair<T,int>(vals[i], i));

    qSort(pairs);
    if (decending) for (int k=0; k<size/2; k++) pairs.swap(k,size-(1+k));

    return pairs;
}


/****
MinMax
    Returns the minimum, maximum, minimum index, and maximum index of a vector of values.
****/
template <typename T>
void MinMax(const QList<T> &vals, T *min, T *max, int *min_index, int *max_index)
{
    const int size = vals.size();
    assert(size > 0);

    *min = *max = vals[0];
    *min_index = *max_index = 0;
    for (int i=1; i<size; i++) {
        const T val = vals[i];
        if (val < *min) {
            *min = val;
            *min_index = i;
        } else if (val > *max) {
            *max = val;
            *max_index = i;
        }
    }
}

template <typename T>
void MinMax(const QList<T> &vals, T *min, T *max)
{
    int min_index, max_index;
    MinMax(vals, min, max, &min_index, &max_index);
}

template <typename T>
T Min(const QList<T> &vals)
{
    int min, max;
    MinMax(vals, &min, &max);
    return min;
}

template <typename T>
T Max(const QList<T> &vals)
{
    int min, max;
    MinMax(vals, &min, &max);
    return max;
}


/****
MeanStdDev
    Returns the mean and standard deviation of a vector of values.
****/
template <typename T>
void MeanStdDev(const QList<T> &vals, double *mean, double *stddev)
{
    const int size = vals.size();

    // Compute Mean
    double sum = 0;
    for (int i=0; i<size; i++) sum += vals[i];
    *mean = (size == 0) ? 0 : sum / size;

    // Compute Standard Deviation
    double variance = 0;
    for (int i=0; i<size; i++) {
        double delta = vals[i] - *mean;
        variance += delta * delta;
    }
    *stddev = (size == 0) ? 0 : sqrt(variance/size);
}


/****
Median
    Computes the median of a list.
****/
template<template<typename> class C, typename T>
T Median(C<T> vals, T *q1 = 0, T *q3 = 0)
{
    if (vals.isEmpty()) return std::numeric_limits<float>::quiet_NaN();
    qSort(vals);
    if (q1 != 0) *q1 = vals[1*vals.size()/4];
    if (q3 != 0) *q3 = vals[3*vals.size()/4];
    return vals[vals.size()/2];
}


/****
Mode
    Computes the mode of a list.
****/
template <typename T>
T Mode(const QList<T> &vals)
{
    QMap<T,int> counts;
    foreach (const T &val, vals) {
        if (!counts.contains(val))
            counts[val] = 0;
        counts[val]++;
    }
    return counts.key(Max(counts.values()));
}


/****
CumSum
    Returns the cumulative sum of a vector of values.
****/
template <typename T>
QList<T> CumSum(const QList<T> &vals)
{
    QList<T> cumsum;
    cumsum.reserve(vals.size()+1);
    cumsum.append(0);
    foreach (const T &val, vals)
        cumsum.append(cumsum.last()+val);
    return cumsum;
}


/****
RandSample
    Returns a vector of n integers sampled in the range <min, max].
    If unique then there will be no repeated integers.
    Note: Algorithm is inefficient for unique vectors where n ~= max-min.
****/
void seedRNG();
QList<int> RandSample(int n, int max, int min = 0, bool unique = false);
QList<int> RandSample(int n, const QSet<int> &values, bool unique = false);

/* Weighted random sample, each entry in weights should be >= 0. */
template <typename T>
QList<int> RandSample(int n, const QList<T> &weights, bool unique = false)
{
    static bool seeded = false;
    if (!seeded) {
        srand(time(NULL));
        seeded = true;
    }

    QList<T> cdf = CumSum(weights);
    for (int i=0; i<cdf.size(); i++) // Normalize cdf
        cdf[i] = cdf[i] / cdf.last();

    QList<int> samples; samples.reserve(n);
    while (samples.size() < n) {
        T r = (T)rand() / (T)RAND_MAX;
        for (int j=0; j<weights.size(); j++) {
            if ((r >= cdf[j]) && (r <= cdf[j+1])) {
                if (!unique || !samples.contains(j))
                    samples.append(j);
                break;
            }
        }
    }

    return samples;
}


/****
Unique
    See Matlab function unique() for documentation.
****/
template <typename T>
void Unique(const QList<T> &vals, QList<T> &b, QList<int> &m, QList<int> &n)
{
    const int size = vals.size();
    assert(size > 0);

    b.reserve(size);
    m.reserve(size);
    n.reserve(size);

    // Compute b and m
    QList< QPair<T, int> > sortedPairs = Sort(vals);
    b.append(sortedPairs[0].first);
    m.append(sortedPairs[0].second);
    for (size_t i=1; i<size; i++) {
        if (sortedPairs[i].first == b.back()) {
            m.back() = qMax(m.back(), sortedPairs[i].second);
        } else {
            b.append(sortedPairs[i].first);
            m.append(sortedPairs[i].second);
        }
    }

    // Compute n
    for (int i=0; i<size; i++) n.append(b.indexOf(vals[i]));
}


/****
SplitPair
    Given a vector of pairs, constructs two new vectors from pair.first and pair.second.
****/
template <typename T, typename U>
void SplitPairs(const QList< QPair<T,U> > &pairs, QList<T> &first, QList<U> &second)
{
    first.reserve(pairs.size());
    second.reserve(pairs.size());
    typedef QPair<T,U> pair_t;
    foreach (const pair_t &pair, pairs) {
        first.append(pair.first);
        second.append(pair.second);
    }
}


/****
RemoveOutliers
    Removes values outside of 1.5 * Inner Quartile Range.
****/
template <typename T>
QList<T> RemoveOutliers(QList<T> vals)
{
    T q1, q3;
    Median(vals, &q1, &q3);
    T iqr = q3-q1;
    T min = q1 - 1.5*iqr;
    T max = q3 + 1.5*iqr;
    QList<T> newVals;
    for (int i=0; i<vals.size(); i++)
        if ((vals[i] >= min) && (vals[i] <= max))
            newVals.append(vals[i]);
    return newVals;
}


/****
Downsample
    Sorts and evenly downsamples a vector to size k.
****/
template <typename T>
QList<T> Downsample(QList<T> vals, long k)
{
    // Use 'long' instead of 'int' so multiplication doesn't overflow
    qSort(vals);
    long size = (long)vals.size();
    if (size <= k) return vals;

    QList<T> newVals; newVals.reserve(k);
    for (long i=0; i<k; i++) newVals.push_back(vals[i * (size-1) / (k-1)]);
    return newVals;
}

}

#endif // __COMMON_H
