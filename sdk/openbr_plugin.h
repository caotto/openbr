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

#ifndef __OPENBR_PLUGIN_H
#define __OPENBR_PLUGIN_H

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVariant>
#include <QVector>
#include <opencv2/core/core.hpp>
#include <openbr.h>

#ifndef BR_EXCEPTIONS
#  define try      if (true)
#  define catch(X) if (false)
#endif // BR_EXCEPTIONS

/*!
 * \defgroup cpp_plugin_sdk C++ Plugin SDK
 * \brief Plugin API for developing new algorithms.
 *
 * \code
 * #include <openbr_plugin.h>
 * \endcode
 *
 * \par Development
 * Plugins should be developed in <tt>sdk/plugins/</tt>.
 * See this folder for numerous examples of existing plugins to follow when writing your own.
 * Plugins may optionally include a <tt>.cmake</tt> file to control build configuration.
 *
 * \par Documentation
 * Plugin documentation should include at least three lines providing the plugin abstraction, a brief explanation, and author.
 * If multiple authors are specified, the last author is assumed to be the current maintainer of the plugin.
 * Plugin authors are encouraged to <tt>\\cite</tt> relevant papers by adding them to <tt>share/openbr/openbr.bib</tt>.
 *
 * \section examples Examples
 * - \ref cpp_compare_faces
 * \subsection cpp_compare_faces Compare Faces
 * \ref cli_compare_faces "Command Line Interface Equivalent"
 * \snippet app/examples/compare_faces.cpp compare_faces
 */

namespace br
{

/*!
 * \addtogroup cpp_plugin_sdk
 *  @{
 */

/*!
 * Helper macro for use with <a href="http://doc.qt.digia.com/qt/properties.html">Q_PROPERTY</a>.
 *
 * \b Example:<br>
 * Note the symmetry between \c BR_PROPERTY and \c Q_PROPERTY.
 * \snippet sdk/plugins/misc.cpp example_transform
 */
#define BR_PROPERTY(TYPE,NAME,DEFAULT)                  \
TYPE NAME;                                              \
TYPE get_##NAME() const { return NAME; }                \
void set_##NAME(TYPE the_##NAME) { NAME = the_##NAME; } \
void reset_##NAME() { NAME = DEFAULT; }

/*!
 * \brief A file path with associated metadata.
 *
 * The br::File is one of the workhorse classes in OpenBR.
 * It is typically used to store the path to a file on disk with associated metadata.
 * The ability to associate a hashtable of metadata with the file helps keep the API simple and stable while providing customizable behavior when appropriate.
 *
 * When querying the value of a metadata key, the value will first try to be resolved using the file's private metadata table.
 * If the key does not exist in the local hashtable then it will be resolved using the properities in the global br::Context.
 * This has the desirable effect that file metadata may optionally set globally using br::Context::set to operate on all files.
 *
 * Files have a simple grammar that allow them to be converted to and from strings.
 * If a string ends with a \c ] or \c ) then the text within the final \c [] or \c () are parsed as comma sperated metadata fields.
 * Fields within \c [] are expected to have the format <tt>[key1=value1, key2=value2, ..., keyN=valueN]</tt>.
 * Fields within \c () are expected to have the format <tt>(value1, value2, ..., valueN)</tt> with the keys determined from the order of \c Q_PROPERTY.
 * The rest of the string is assigned to #name.
 *
 * Metadata keys fall into one of two categories:
 * - \c camelCaseKeys are inputs that specify how to process the file.
 * - \c Capitalized_Underscored_Keys are outputs computed from processing the file.
 *
 * Below are some of the most commonly occuring standardized keys:
 *
 * Key             | Value          | Description
 * ---             | ----           | -----------
 * path            | QString        | Resolve complete file paths from file names
 * enrollAll       | bool           | Enroll zero or more templates per file
 * separator       | QString        | Sperate #name into multiple files
 * Input_Index     | int            | Index of a template in a template list
 * Label           | float          | Classification/Regression class
 * Confidence      | float          | Classification/Regression quality
 * FTE             | bool           | Failure to enroll
 * FTO             | bool           | Failure to open
 * *_X             | float          | Position
 * *_Y             | float          | Position
 * *_Width         | float          | Size
 * *_Height        | float          | Size
 * *_Radius        | float          | Size
 * Theta           | float          | Pose
 * Roll            | float          | Pose
 * Pitch           | float          | Pose
 * Yaw             | float          | Pose
 * Landmarks       | QList<QPointF> | Landmark list
 * ROIs            | QList<Rect>    | Region Of Interest (ROI) list
 * _*              | *              | Reserved for internal use
 */
struct BR_EXPORT File
{
    QString name; /*!< \brief Path to a file on disk. */

    File() {}
    File(const QString &file) { init(file); } /*!< \brief Construct a file from a string. */
    File(const QString &file, const QVariant &label) { init(file); insert("Label", label); } /*!< \brief Construct a file from a string and assign a label. */
    File(const char *file) { init(file); } /*!< \brief Construct a file from a c-style string. */
    operator QString() const { return name; } /*!< \brief Returns #name. */
    QString flat() const; /*!< \brief A stringified version of the file with metadata. */
    QString hash() const; /*!< \brief A hash of the file. */
    inline void clear() { name.clear(); m_metadata.clear(); } /*!< \brief Clears the file's name and metadata. */

    inline QList<QString> localKeys() const { return m_metadata.keys(); } /*!< \brief Returns the private metadata keys. */
    inline QHash<QString,QVariant> localMetadata() const { return m_metadata; } /*!< \brief Returns the private metadata. */
    inline void insert(const QString &key, const QVariant &value) { set(key, value); } /*!< \brief Equivalent to set(). */
    void append(const QHash<QString,QVariant> &localMetadata); /*!< \brief Add new metadata fields. */
    void append(const File &other); /*!< \brief Append another file using \c separator. */
    QList<File> split() const; /*!< \brief Split the file using \c separator. */
    QList<File> split(const QString &separator) const; /*!< \brief Split the file. */

    inline void insertParameter(int index, const QVariant &value) { insert("_Arg" + QString::number(index), value); } /*!< \brief Insert a keyless value. */
    inline bool containsParameter(int index) const { return m_metadata.contains("_Arg" + QString::number(index)); } /*!< \brief Check for the existence of a keyless value. */
    inline QVariant parameter(int index) const { return m_metadata.value("_Arg" + QString::number(index)); } /*!< \brief Retrieve a keyless value. */

    inline bool operator==(const char* other) const { return name == other; } /*!< \brief Compare name to c-style string. */
    inline bool operator==(const File &other) const { return (name == other.name) && (m_metadata == other.m_metadata); } /*!< \brief Compare name and metadata for equality. */
    inline bool operator!=(const File &other) const { return !(*this == other); } /*!< \brief Compare name and metadata for inequality. */
    inline bool operator<(const File &other) const { return name < other.name; } /*!< \brief Compare name. */
    inline bool operator<=(const File &other) const { return name <= other.name; } /*!< \brief Compare name. */
    inline bool operator>(const File &other) const { return name > other.name; } /*!< \brief Compare name. */
    inline bool operator>=(const File &other) const { return name >= other.name; } /*!< \brief Compare name. */
    inline File &operator+=(const QHash<QString,QVariant> &other) { append(other); return *this; } /*!< \brief Add new metadata fields. */
    inline File &operator+=(const File &other) { append(other); return *this; } /*!< \brief Append another file using \c separator. */

    inline bool isNull() const { return name.isEmpty() && m_metadata.isEmpty(); } /*!< \brief Returns \c true if name and metadata are empty, \c false otherwise. */
    inline bool isTerminal() const { return name == "terminal"; } /*!< \brief Returns \c true if #name is "terminal", \c false otherwise. */
    inline bool exists() const { return QFileInfo(name).exists(); } /*!< \brief Returns \c true if the file exists on disk, \c false otherwise. */
    inline QString fileName() const { return QFileInfo(name).fileName(); } /*!< \brief Returns the file's base name and extension. */
    inline QString baseName() const { const QString baseName = QFileInfo(name).baseName();
                                      return baseName.isEmpty() ? QDir(name).dirName() : baseName; } /*!< \brief Returns the file's base name. */
    inline QString suffix() const { return QFileInfo(name).suffix(); } /*!< \brief Returns the file's extension. */

    bool contains(const QString &key) const; /*!< \brief Returns \c true if the key has an associated value, \c false otherwise. */
    QVariant value(const QString &key) const; /*!< \brief Returns the value for the specified key. */
    static QString subject(int label); /*!< \brief Looks up the subject for the provided label. */
    inline QString subject() const { return subject(label()); } /*!< \brief Looks up the subject from the file's label. */
    inline bool failed() const { return getBool("FTE") || getBool("FTO"); } /*!< \brief Returns \c true if the file failed to open or enroll, \c false otherwise. */

    void set(const QString &key, const QVariant &value); /*!< \brief Insert or overwrite the metadata key with the specified value. */
    QVariant get(const QString &key) const; /*!< \brief Returns a QVariant for the key, throwing an error if the key does not exist. */
    QVariant get(const QString &key, const QVariant &value) const; /*!< \brief Returns a QVariant for the key, returning \em defaultValue if the key does not exist. */
    float label() const; /*!< \brief Convenience function for retrieving the file's \c Label. */
    inline void setLabel(float label) { insert("Label", label); } /*!< \brief Convenience function for setting the file's \c Label. */
    bool getBool(const QString &key) const; /*!< \brief Returns a boolean value for the key. */
    void setBool(const QString &key, bool value = true); /*!< \brief Sets a boolean value for the key. */
    int getInt(const QString &key) const; /*!< \brief Returns an int value for the key, throwing an error if the key does not exist. */
    int getInt(const QString &key, int defaultValue) const; /*!< \brief Returns an int value for the key, returning \em defaultValue if the key does not exist. */
    float getFloat(const QString &key) const; /*!< \brief Returns a float value for the key, throwing an error if the key does not exist. */
    float getFloat(const QString &key, float defaultValue) const; /*!< \brief Returns a float value for the key, returning \em defaultValue if the key does not exist. */
    QString getString(const QString &key) const; /*!< \brief Returns a string value for the key, throwing an error if the key does not exist. */
    QString getString(const QString &key, const QString &defaultValue) const; /*!< \brief Returns a string value for the key, returning \em defaultValue if the key does not exist. */

    QList<QPointF> landmarks() const; /*!< \brief Returns the file's landmark list. */
    void appendLandmark(const QPointF &landmark); /*!< \brief Adds a landmark to the file's landmark list. */
    void appendLandmarks(const QList<QPointF> &landmarks); /*!< \brief Adds landmarks to the file's landmark list. */
    inline void clearLandmarks() { m_metadata["Landmarks"] = QList<QVariant>(); } /*!< \brief Clears the file's landmark list. */
    void setLandmarks(const QList<QPointF> &landmarks); /*!< \brief Assigns the file's landmark list. */

    QList<QRectF> ROIs() const; /*!< \brief Returns the file's ROI list. */
    void appendROI(const QRectF &ROI); /*!< \brief Adds a ROI to the file's ROI list. */
    void appendROIs(const QList<QRectF> &ROIs); /*!< \brief Adds ROIs to the file's ROI list. */
    inline void clearROIs() { m_metadata["ROIs"] = QList<QVariant>(); } /*!< \brief Clears the file's landmark list. */
    void setROIs(const QList<QRectF> &ROIs); /*!< \brief Assigns the file's landmark list. */

private:
    QHash<QString,QVariant> m_metadata;
    BR_EXPORT friend QDataStream &operator<<(QDataStream &stream, const File &file); /*!< */
    BR_EXPORT friend QDataStream &operator>>(QDataStream &stream, File &file); /*!< */

    void init(const QString &file);
};

BR_EXPORT QDebug operator<<(QDebug dbg, const File &file); /*!< \brief Prints br::File::flat() to \c stderr. */
BR_EXPORT QDataStream &operator<<(QDataStream &stream, const File &file); /*!< \brief Serializes the file to a stream. */
BR_EXPORT QDataStream &operator>>(QDataStream &stream, File &file); /*!< \brief Deserializes the file from a stream. */

/*!
 * \brief A list of files.
 *
 * Convenience class for working with a list of files.
 */
struct BR_EXPORT FileList : public QList<File>
{
    FileList() {}
    FileList(int n); /*!< \brief Initialize the list with \em n empty files. */
    FileList(const QStringList &files); /*!< \brief Initialize the file list from a string list. */
    FileList(const QList<File> &files) { append(files); } /*!< \brief Initialize the file list from another file list. */

    QStringList flat() const; /*!< \brief Returns br::File::flat() for each file in the list. */
    QStringList names() const; /*!<  \brief Returns #br::File::name for each file in the list. */
    QList<float> labels() const; /*!< \brief Returns br::File::label() for each file in the list. */
    int failures() const; /*!< \brief Returns the number of files with br::File::failed(). */
};

/*!
 * \brief A list of matrices associated with a file.
 *
 * The br::Template is one of the workhorse classes in OpenBR.
 * A template represents a biometric at various stages of enrollment and can be modified br::Transform and compared to other templates with br::Distance.
 *
 * While there exist many cases (ex. video enrollment, multiple face detects, per-patch subspace learning, ...) where the template will contain more than one matrix,
 * in most cases templates have exactly one matrix in their list representing a single image at various stages of enrollment.
 * In the cases where exactly one image is expected, the template provides the function m() as an idiom for treating it as a single matrix.
 * Casting operators are also provided to pass the template into image processing functions expecting matrices.
 *
 * Metadata related to the template that is computed during enrollment (ex. bounding boxes, eye locations, quality metrics, ...) should be assigned to the template's #file member.
 */
struct Template : public QList<cv::Mat>
{
    File file; /*!< \brief The file from which the template is constructed. */
    Template() {}
    Template(const File &_file) : file(_file) {} /*!< \brief Initialize #file. */
    Template(const File &_file, const cv::Mat &mat) : file(_file) { append(mat); } /*!< \brief Initialize #file and append a matrix. */
    Template(const cv::Mat &mat) { append(mat); } /*!< \brief Append a matrix. */

    inline const cv::Mat &m() const { static const cv::Mat NullMatrix;
                                      return isEmpty() ? qFatal("Template::m() empty template."), NullMatrix : last(); } /*!< \brief Idiom to treat the template as a matrix. */
    inline cv::Mat &m() { return isEmpty() ? append(cv::Mat()), last() : last(); } /*!< \brief Idiom to treat the template as a matrix. */
    inline cv::Mat &operator=(const cv::Mat &other) { return m() = other; } /*!< \brief Idiom to treat the template as a matrix. */
    inline operator const cv::Mat&() const { return m(); } /*!< \brief Idiom to treat the template as a matrix. */
    inline operator cv::Mat&() { return m(); } /*!< \brief Idiom to treat the template as a matrix. */
    inline operator cv::_InputArray() const { return m(); } /*!< \brief Idiom to treat the template as a matrix. */
    inline operator cv::_OutputArray() { return m(); } /*!< \brief Idiom to treat the template as a matrix. */
    inline bool isNull() const { return isEmpty() || !m().data; } /*!< \brief Returns \c true if the template is empty or has no matrix data, \c false otherwise. */
    inline void merge(const Template &other) { append(other); file.append(other.file); } /*!< \brief Append the contents of another template. */

    /*!
     * \brief Returns the total number of bytes in all the matrices.
     */
    size_t bytes() const
    {
        size_t bytes = 0;
        foreach (const cv::Mat &m, *this)
            bytes += m.total() * m.elemSize();
        return bytes;
    }

    /*!
     * \brief Copies all the matrices and returns a new template.
     */
    Template clone() const
    {
        Template other(file);
        foreach (const cv::Mat &m, *this) other += m.clone();
        return other;
    }
};

/*!
 * \brief A list of templates.
 *
 * Convenience class for working with a list of templates.
 */
struct TemplateList : public QList<Template>
{
    bool uniform; /*!< \brief Reserved for internal use. True if all templates are aligned and of the same size and type. */
    QVector<uchar> alignedData; /*!< \brief Reserved for internal use. */

    TemplateList() : uniform(false) {}
    TemplateList(const QList<Template> &templates) : uniform(false) { append(templates); } /*!< \brief Initialize the template list from another template list. */
    BR_EXPORT static TemplateList fromInput(const File &input); /*!< \brief Create a template list from a br::Input. */

    /*!
     * \brief Returns the total number of bytes in all the templates.
     */
    template <typename T>
    T bytes() const
    {
        T bytes = 0;
        foreach (const Template &t, *this) bytes += t.bytes();
        return bytes;
    }

    /*!
     * \brief Returns a list of matrices with one matrix from each template at the specified \em index.
     */
    QList<cv::Mat> data(int index = 0) const
    {
        QList<cv::Mat> data; data.reserve(size());
        foreach (const Template &t, *this) data.append(t[index]);
        return data;
    }

    /*!
     * \brief Returns #br::Template::file for each template in the list.
     */
    FileList files() const
    {
        FileList files; files.reserve(size());
        foreach (const Template &t, *this) files.append(t.file);
        return files;
    }

    /*!
     * \brief Returns br::Template::label() for each template in the list.
     */
    template <typename T>
    QList<T> labels() const
    {
        QList<T> labels; labels.reserve(size());
        foreach (const Template &t, *this) labels.append(t.file.label());
        return labels;
    }

    /*!
     * \brief Returns the number of occurences for each label in the list.
     */
    QMap<int,int> labelCounts(bool excludeFailures = false) const
    {
        QMap<int, int> labelCounts;
        foreach (const File &file, files())
            if (!excludeFailures || !file.failed())
                labelCounts[file.label()]++;
        return labelCounts;
    }
};

/*!
 * \brief The base class of all plugins and objects requiring introspection.
 *
 * Plugins are constructed from files.
 * The file's name specifies which plugin to construct and the metadata provides initialization values for the plugin's properties.
 */
class BR_EXPORT Object : public QObject
{
    Q_OBJECT

public:
    File file; /*!< \brief The file used to construct the plugin. */

    virtual QString name() const; /*!< \brief The plugin class name. */
    virtual void init() {} /*!< \brief Overload this function instead of the default constructor to initialize the derived class. It should be safe to call this function multiple times. */
    virtual void store(QDataStream &stream) const; /*!< \brief Serialize the object. */
    virtual void load(QDataStream &stream); /*!< \brief Deserialize the object. Default implementation calls init() after deserialization. */

    QStringList parameters() const; /*!< \brief A string describing the parameters the object takes. */
    QStringList arguments() const; /*!< \brief A string describing the values the object has. */
    QString argument(int index) const; /*!< \brief A string value for the argument at the specified index. */
    QString description() const; /*!< \brief Returns a string description of the object. */
    void setProperty(const QString &name, const QString &value); /*!< \brief Overload of QObject::setProperty to handle OpenBR data types. */
    static QStringList parse(const QString &string, char split = ','); /*!< \brief Splits the string while respecting lexical scoping of <tt>()</tt>, <tt>[]</tt>, <tt>\<\></tt>, and <tt>{}</tt>. */

private:
    template <typename T> friend struct Factory;
    void init(const File &file); /*!< \brief Initializes the plugin's properties from the file's metadata. */
};

/*!
 * \brief The singleton class of global settings.
 *
 * Allocated by br::Context::initialize(), and deallocated by br::Context::finalize().
 *
 * \code
 * // Access the global context using the br::Globals pointer:
 * QString theSDKPath = br::Globals->sdkPath;
 * \endcode
 */
class BR_EXPORT Context : public Object
{
    Q_OBJECT
    QSharedPointer<QCoreApplication> coreApplication;
    QFile logFile;

public:
    /*!
     * \brief Path to <tt>share/openbr/openbr.bib</tt>
     */
    Q_PROPERTY(QString sdkPath READ get_sdkPath WRITE set_sdkPath RESET reset_sdkPath)
    BR_PROPERTY(QString, sdkPath, "")

    /*!
     * \brief The default algorithm to use when enrolling and comparing templates.
     */
    Q_PROPERTY(QString algorithm READ get_algorithm WRITE set_algorithm RESET reset_algorithm)
    BR_PROPERTY(QString, algorithm, "")

    /*!
     * \brief Optional log file to copy <tt>stderr</tt> to.
     */
    Q_PROPERTY(QString log READ get_log WRITE set_log RESET reset_log)
    BR_PROPERTY(QString, log, "")

    /*!
     * \brief Path to use when resolving images specified with relative paths.
     */
    Q_PROPERTY(QString path READ get_path WRITE set_path RESET reset_path)
    BR_PROPERTY(QString, path, "")

    /*!
     * \brief The maximum number of templates to process in parallel.
     */
    Q_PROPERTY(int blockSize READ get_blockSize WRITE set_blockSize RESET reset_blockSize)
    BR_PROPERTY(int, blockSize, 1)

    /*!
     * \brief The number of threads to use.
     */
    Q_PROPERTY(int parallelism READ get_parallelism WRITE set_parallelism RESET reset_parallelism)
    BR_PROPERTY(int, parallelism, 0)

    /*!
     * \brief If \c true no messages will be sent to the terminal, \c false by default.
     */
    Q_PROPERTY(bool quiet READ get_quiet WRITE set_quiet RESET reset_quiet)
    BR_PROPERTY(bool, quiet, false)

    /*!
     * \brief If \c true extra messages will be sent to the terminal, \c false by default.
     */
    Q_PROPERTY(bool verbose READ get_verbose WRITE set_verbose RESET reset_verbose)
    BR_PROPERTY(bool, verbose, false)

    /*!
     * \brief The most resent message sent to the terminal.
     */
    Q_PROPERTY(QString mostRecentMessage READ get_mostRecentMessage WRITE set_mostRecentMessage RESET reset_mostRecentMessage)
    BR_PROPERTY(QString, mostRecentMessage, "")

    /*!
     * \brief Used internally to compute progress() and timeRemaining().
     */
    Q_PROPERTY(double currentStep READ get_currentStep WRITE set_currentStep RESET reset_currentStep)
    BR_PROPERTY(double, currentStep, 0)

    /*!
     * \brief Used internally to compute progress() and timeRemaining().
     */
    Q_PROPERTY(double totalSteps READ get_totalSteps WRITE set_totalSteps RESET reset_totalSteps)
    BR_PROPERTY(double, totalSteps, 0)

    /*!
     * \brief If \c true enroll 0 or more templates per image, otherwise (default) enroll exactly one.
     */
    Q_PROPERTY(bool enrollAll READ get_enrollAll WRITE set_enrollAll RESET reset_enrollAll)
    BR_PROPERTY(bool, enrollAll, false)

    QHash<QString,QString> abbreviations; /*!< \brief Used by br::Transform::make() to expand abbreviated algorithms into their complete definitions. */
    QHash<QString,int> classes; /*!< \brief Used by classifiers to associate text class labels with unique integers IDs. */
    QTime startTime; /*!< \brief Used to estimate timeRemaining(). */

    Context();

    /*!
     * \brief Returns the suggested number of partitions \em size should be divided into for processing.
     * \param size The length of the list to be partitioned.
     */
    int blocks(int size) const;

    /*!
     * \brief Returns true if \em name is queryable using <a href="http://doc.qt.digia.com/qt/qobject.html#property">QObject::property</a>
     * \param name The property key to check for existance.
     * \return \c true if \em name is a property, \c false otherwise.
     * \see set
     */
    bool contains(const QString &name);

    /*!
     * \brief Prints current progress statistics to \em stdout.
     * \see progress
     */
    void printStatus();

    /*!
     * \brief Returns the completion percentage of a call to br::Train(), br::Enroll() or br::Compare().
     * \return float Fraction completed.
     *  - \c -1 if no task is underway.
     * \see timeRemaining
     */
    float progress() const;

    /*!
     * \brief Set a global property.
     * \param key Global property key.
     * \param value Global property value.
     * \see contains
     */
    void setProperty(const QString &key, const QString &value);

    /*!
     * \brief Returns the time remaining in seconds of a call to \ref br_train, \ref br_enroll or \ref br_compare.
     * \return int Time remaining in seconds.
     *  - \c -1 if no task is underway.
     * \see progress
     */
    int timeRemaining() const;

    /*!
     * \brief Continues to print the progress of the futures until they are completed.
     * \param futures The list of futures to track.
     */
    void trackFutures(QList< QFuture<void> > &futures);

    /*!
     * \brief Returns \c true if \em sdkPath is valid, \c false otherwise.
     * \param sdkPath The path to <tt>share/openbr/openbr.bib</tt>
     */
    static bool checkSDKPath(const QString &sdkPath);

    /*!
     * \brief Call \em once at the start of the application to allocate global variables.
     * \code
     * int main(int argc, char *argv[])
     * {
     *     br::Context::initialize(argc, argv);
     *
     *     // ...
     *
     *     br::Context::finalize();
     *     return 0;
     * }
     * \endcode
     * \param argc As provided by <tt>main()</tt>.
     * \param argv As provided by <tt>main()</tt>.
     * \param sdkPath The path to the folder containing <tt>share/openbr/openbr.bib</tt>
     *                 By default <tt>share/openbr/openbr.bib</tt> will be searched for relative to:
     *                   -# The working directory
     *                   -# The executable's location
     * \note Tiggers \em abort() on failure to locate <tt>share/openbr/openbr.bib</tt>.
     * \note <a href="http://qt-project.org/">Qt</a> users should instead call initializeQt().
     * \see initializeQt finalize
     */
    static void initialize(int argc, char *argv[], const QString &sdkPath = "");

    /*!
     * \brief Alternative to initialize() for <a href="http://qt-project.org/">Qt</a> users.
     *
     * This alternative to initialize() should be used when a
     * <a href="http://doc.qt.digia.com/stable/qcoreapplication.html">QCoreApplication</a>
     * has already been defined.
     * \code
     * int main(int argc, char *argv[])
     * {
     *     QApplication a(argc, argv);
     *     br::Context::initializeQt();
     *
     *     // ...
     *
     *     int result = a.exec();
     *     br::Context::finalize();
     *     return result;
     * }
     * \endcode
     * \see initialize
     */
    static void initializeQt(QString sdkPath);

    /*!
     * \brief Call \em once at the end of the application to deallocate global variables.
     * \see initialize
     */
    static void finalize();

    /*!
     * \brief Returns a string with the name, version, and copyright of the project.
     * \return A string suitable for printing to the terminal or displaying in a dialog box.
     * \see version
     */
    static QString about();

    /*!
     * \brief Returns the version of the SDK.
     * \return A string with the format: <i>\<MajorVersion\></i><tt>.</tt><i>\<MinorVersion\></i><tt>.</tt><i>\<PatchVersion\></i>
     * \see about scratchPath
     */
    static QString version();

    /*!
     * \brief Returns the scratch directory.
     * \return A string with the format: <i>\</path/to/user/home/\></i><tt>OpenBR-</tt><i>\<MajorVersion\></i><tt>.</tt><i>\<MinorVersion\></i>
     * \note This should be used as the root directory for managing temporary files and providing process persistence.
     * \see version
     */
    static QString scratchPath();

private:
    static void messageHandler(QtMsgType type, const char *msg);
};

/*!
 * \brief The globally available settings.
 *
 * Initialized by Context::initialize() and destroyed with Context::finalize().
 */
BR_EXPORT extern Context *Globals;

/*!
 * \brief For run time construction of objects from strings.
 *
 * All plugins must derive from br::Object.
 * The factory is a templated struct to allow for different types of plugins.
 *
 * Uses the Industrial Strength Pluggable Factory model described <a href="http://adtmag.com/articles/2000/09/25/industrial-strength-pluggable-factories.aspx">here</a>.
 */
template <class T>
struct Factory
{
    virtual ~Factory() {}

    /*!
     * \brief Constructs a plugin from a file.
     */
    static T *make(const File &file)
    {
        QString name = file.suffix();
        if (!names().contains(name)) {
            if      (names().contains("Empty") && name.isEmpty()) name = "Empty";
            else if (names().contains("Default"))                 name = "Default";
            else    qFatal("%s registry does not contain object named: %s", qPrintable(baseClassName()), qPrintable(name));
        }
        if (registry->contains("_"+name)) name.prepend('_'); // Hook to override with "native" implementation
        T *object = registry->value(name)->_make();
        object->init(file);
        return object;
    }

    /*!
     * \brief Constructs all the available plugins.
     */
    static QList< QSharedPointer<T> > makeAll()
    {
        QList< QSharedPointer<T> > objects;
        foreach (const QString &name, names()) {
            objects.append(QSharedPointer<T>(registry->value(name)->_make()));
            objects.last()->init("");
        }
        return objects;
    }

    /*!
     * \brief Returns the names of the available plugins.
     */
    static QStringList names() { return registry ? registry->keys() : QStringList(); }

    /*!
     * \brief Returns the parameters for a plugin.
     */
    static QString parameters(const QString &name)
    {
        if (!registry) return QString();
        QScopedPointer<T> object(registry->value(name)->_make());
        object->init(name);
        return object->parameters().join(", ");
    }

protected:
    /*!
     * \brief For internal use. Nifty trick to register objects using a constructor.
     */
    Factory(QString name)
    {
        if (!registry) registry = new QMap<QString,Factory<T>*>();

        const QString abstraction = baseClassName();
        if (name.endsWith(abstraction)) name = name.left(name.size()-abstraction.size());
        if (registry->contains(name)) qFatal("%s registry already contains object named: %s", qPrintable(abstraction), qPrintable(name));
        registry->insert(name, this);
    }

private:
    static QMap<QString,Factory<T>*> *registry;

    static QString baseClassName() { return QString(T::staticMetaObject.className()).remove("br::"); }
    virtual T *_make() const = 0;
};

template <class T> QMap<QString, Factory<T>*>* Factory<T>::registry = 0;

template <class _Abstraction, class _Implementation>
class FactoryInstance : public Factory<_Abstraction>
{
    FactoryInstance() : Factory<_Abstraction>(_Implementation::staticMetaObject.className()) {}
    _Abstraction *_make() const { return new _Implementation(); }
    static const FactoryInstance registerThis;
};
template <class _Abstraction, class _Implementation>
const FactoryInstance<_Abstraction,_Implementation> FactoryInstance<_Abstraction,_Implementation>::registerThis;

/*!
 * Macro to register a br::Factory plugin.
 *
 * \b Example:<br>
 * Note the use of \c Q_OBJECT at the beginning of the class declaration and \c BR_REGISTER after the class declaration.
 * \snippet sdk/plugins/misc.cpp example_transform
 */
#define BR_REGISTER(ABSTRACTION,IMPLEMENTATION)       \
template class                                        \
br::FactoryInstance<br::ABSTRACTION, IMPLEMENTATION>;

/*!
 * \defgroup initializers Initializers
 * \brief Plugins that initialize resources.
 */

/*!
 * \ingroup initializers
 * \brief Plugin base class for initializing resources.
 */
class BR_EXPORT Initializer : public Object
{
    Q_OBJECT

public:
    virtual ~Initializer() {}
    virtual void initialize() const = 0;  /*!< \brief Called once at the end of br::Context::initialize(). */
    virtual void finalize() const {}  /*!< \brief Called once at the beginning of br::Context::finalize(). */
};

/*!
 * \defgroup outputs Outputs
 * \brief Plugins that store template comparison results.
 */

/*!
 * \ingroup outputs
 * \brief Plugin base class for storing template comparison results.
 *
 * An \em output is a br::File representing the result comparing templates.
 * br::File::suffix() is used to determine which plugin should handle the output.
 * \note Handle serialization to disk in the derived class destructor.
 */
class BR_EXPORT Output : public Object
{
    Q_OBJECT

public:
    FileList targetFiles; /*!< \brief List of files representing the gallery templates. */
    FileList queryFiles; /*!< \brief List of files representing the probe templates. */
    bool selfSimilar; /*!< \brief \c true if the \em targetFiles == \em queryFiles, \c false otherwise. */

    virtual ~Output() {}
    void setBlock(int rowBlock, int columnBlock); /*!< \brief Set the current block. */
    void setRelative(float value, int i, int j); /*!< \brief Set a score relative to the current block. */

    static Output *make(const File &file, const FileList &targetFiles, const FileList &queryFiles); /*!< \brief Make an output from a file and gallery/probe file lists. */
    static void reformat(const FileList &targetFiles, const FileList &queryFiles, const File &simmat, const File &output); /*!< \brief Create an output from a similarity matrix and file lists. */

protected:
    virtual void initialize(const FileList &targetFiles, const FileList &queryFiles); /*!< \brief Initializes class data members. */

private:
    QSharedPointer<Output> next;
    QPoint offset;
    virtual void set(float value, int i, int j) = 0;
};

/*!
 * \ingroup outputs
 * \brief Plugin derived base class for storing outputs as matrices.
 */
class BR_EXPORT MatrixOutput : public Output
{
    Q_OBJECT

public:
    cv::Mat data; /*!< \brief The similarity matrix. */

protected:
    QString toString(int row, int column) const; /*!< \brief Converts the value requested similarity score to a string. */

private:
    void initialize(const FileList &targetFiles, const FileList &queryFiles);
    void set(float value, int i, int j);
};

/*!
 * \defgroup formats Formats
 * \brief Plugins that read a matrix.
 */

/*!
 * \ingroup formats
 * \brief Plugin base class for reading matrices from disk.
 *
 * A \em format is a br::File representing a matrix (ex. jpg image) on disk.
 * br::File::suffix() is used to determine which plugin should handle the format.
 */
class BR_EXPORT Format : public Object
{
    Q_OBJECT

public:
    virtual ~Format() {}
    virtual QList<cv::Mat> read() const = 0; /*!< \brief Returns a list of matrices created by reading #br::Object::file. */
};

/*!
 * \defgroup galleries Galleries
 * \brief Plugins that store templates.
 */

/*!
 * \ingroup galleries
 * \brief Plugin base class for storing a list of enrolled templates.
 *
 * A \em gallery is a file representing a br::TemplateList serialized to disk.
 * br::File::suffix() is used to determine which plugin should handle the gallery.
 * \note Handle serialization to disk in the derived class destructor.
 */
class BR_EXPORT Gallery : public Object
{
    Q_OBJECT

public:
    virtual ~Gallery() {}
    virtual bool isUniversal() const = 0; /*!< \c true if the gallery can read and write complete templates, \c false otherwise. */
    TemplateList read(); /*!< \brief Retrieve all the stored templates. */
    FileList files(); /*!< \brief Retrieve all the stored template files. */
    virtual TemplateList readBlock(bool *done) = 0; /*!< \brief Retrieve a portion of the stored templates. */
    void writeBlock(const TemplateList &templates); /*!< \brief Serialize a template list. */
    virtual void write(const Template &t) = 0; /*!< \brief Serialize a template. */
    static Gallery *make(const File &file); /*!< \brief Make a gallery from a file list. */

private:
    QSharedPointer<Gallery> next;
};

/*!
 * \defgroup transforms Transforms
 * \brief Plugins that process a template.
 */

/*!
 * \addtogroup transforms
 *  @{
 */

/*!
 * \brief Plugin base class for processing a template.
 *
 * Transforms support the idea of \em training and \em projecting,
 * whereby they are (optionally) given example images and are expected learn how to transform new instances into an alternative,
 * hopefully more useful, basis for the recognition task at hand.
 * Transforms can be chained together to support the declaration and use of arbitrary algorithms at run time.
 */
class BR_EXPORT Transform : public Object
{
    Q_OBJECT
    bool independent;

public:
    Q_PROPERTY(bool relabel READ get_relabel WRITE set_relabel RESET reset_relabel STORED false)
    Q_PROPERTY(int classes READ get_classes WRITE set_classes RESET reset_classes STORED false)
    Q_PROPERTY(int instances READ get_instances WRITE set_instances RESET reset_instances STORED false)
    Q_PROPERTY(float fraction READ get_fraction WRITE set_fraction RESET reset_fraction STORED false)
    BR_PROPERTY(bool, relabel, false)
    BR_PROPERTY(int, classes, std::numeric_limits<int>::max())
    BR_PROPERTY(int, instances, std::numeric_limits<int>::max())
    BR_PROPERTY(float, fraction, 1)

    virtual ~Transform() {}
    static Transform *make(QString str, QObject *parent); /*!< \brief Make a transform from a string. */
    static QSharedPointer<Transform> fromAlgorithm(const QString &algorithm); /*!< \brief Retrieve an algorithm's transform. */

    virtual Transform *clone() const; /*!< \brief Copy the transform. */
    virtual void train(const TemplateList &data) = 0; /*!< \brief Train the transform. */
    virtual void project(const Template &src, Template &dst) const = 0; /*!< \brief Apply the transform. */
    virtual void project(const TemplateList &src, TemplateList &dst) const; /*!< \brief Apply the transform. */

    /*!
     * \brief Convenience function equivalent to project().
     */
    inline Template operator()(const Template &src) const
    {
        Template dst;
        dst.file = src.file;
        project(src, dst);
        return dst;
    }

    /*!
     * \brief Convenience function equivalent to project().
     */
    inline TemplateList operator()(const TemplateList &src) const
    {
        TemplateList dst;
        project(src, dst);
        return dst;
    }

protected:
    Transform(bool independent = true); /*!< \brief Construct a transform. */
    inline Transform *make(const QString &description) { return make(description, this); } /*!< \brief Make a subtransform. */
};

/*!
 * \brief Convenience function equivalent to project().
 */
inline Template &operator>>(Template &srcdst, const Transform &f)
{
    srcdst = f(srcdst);
    return srcdst;
}

/*!
 * \brief Convenience function equivalent to project().
 */
inline TemplateList &operator>>(TemplateList &srcdst, const Transform &f)
{
    srcdst = f(srcdst);
    return srcdst;
}

/*!
 * \brief Convenience function equivalent to store().
 */
inline QDataStream &operator<<(QDataStream &stream, const Transform &f)
{
    f.store(stream);
    return stream;
}

/*!
 * \brief Convenience function equivalent to load().
 */
inline QDataStream &operator>>(QDataStream &stream, Transform &f)
{
    f.load(stream);
    return stream;
}

/*!
 * \brief A br::Transform expecting multiple matrices per template.
 */
class BR_EXPORT MetaTransform : public Transform
{
    Q_OBJECT

protected:
    MetaTransform() : Transform(false) {}
};

/*!
 * \brief A br::Transform that does not require training data.
 */
class BR_EXPORT UntrainableTransform : public Transform
{
    Q_OBJECT

protected:
    UntrainableTransform(bool independent = true) : Transform(independent) {} /*!< \brief Construct an untrainable transform. */

private:
    Transform *clone() const { return const_cast<UntrainableTransform*>(this); }
    void train(const TemplateList &data) { (void) data; }
    void store(QDataStream &stream) const { (void) stream; }
    void load(QDataStream &stream) { (void) stream; }
};

/*!
 * \brief A br::MetaTransform that does not require training data.
 */
class BR_EXPORT UntrainableMetaTransform : public UntrainableTransform
{
    Q_OBJECT

protected:
    UntrainableMetaTransform() : UntrainableTransform(false) {}
};

/*! @}*/

/*!
 * \defgroup distances Distances
 * \brief Plugins that compare templates.
 */

/*!
 * \ingroup distances
 * \brief Plugin base class for comparing templates.
 */
class BR_EXPORT Distance : public Object
{
    Q_OBJECT

    // Score normalization
    Q_PROPERTY(float a READ get_a WRITE set_a RESET reset_a)
    Q_PROPERTY(float b READ get_b WRITE set_b RESET reset_b)
    BR_PROPERTY(float, a, 1)
    BR_PROPERTY(float, b, 0)

public:
    static QSharedPointer<Distance> fromAlgorithm(const QString &algorithm); /*!< \brief Retrieve an algorithm's distance. */
    virtual void train(const TemplateList &src); /*!< \brief Train the distance. */
    virtual void compare(const TemplateList &target, const TemplateList &query, Output *output) const; /*!< \brief Compare two template lists. */
    inline float compare(const Template &target, const Template &query) const { return a * (_compare(target, query) - b); } /*!< \brief Compute the normalized distance between two templates. */

private:
    virtual void compareBlock(const TemplateList &target, const TemplateList &query, Output *output, int targetOffset, int queryOffset) const;
    virtual float _compare(const Template &a, const Template &b) const = 0; /*!< \brief Compute the distance between two templates. */
};

/*!
* \brief Returns \c true if the algorithm is a classifier, \c false otherwise.
*
* Classifers have no br::Distance associated with their br::Transform.
* Instead they populate br::Template::file \c Label metadata fielf with the predicted class.
*/
BR_EXPORT bool IsClassifier(const QString &algorithm);

/*!
 * \brief High-level function for creating models.
 * \see br_train
 */
BR_EXPORT void Train(const QString &inputs, const File &model);

/*!
 * \brief High-level function for creating galleries.
 * \see br_enroll
 */
BR_EXPORT FileList Enroll(const File &input, const File &gallery = File());

/*!
 * \brief High-level function for comparing galleries.
 * \see br_compare
 */
BR_EXPORT void Compare(const File &targetGallery, const File &queryGallery, const File &output);

/*! @}*/

} // namespace br

Q_DECLARE_METATYPE(QList<float>)
Q_DECLARE_METATYPE(QList<int>)
Q_DECLARE_METATYPE(br::Transform*)
Q_DECLARE_METATYPE(QList<br::Transform*>)
Q_DECLARE_METATYPE(cv::Mat)

#endif // __OPENBR_PLUGIN_H
