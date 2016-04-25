/*
    Copyright © 2014-2015 by The qTox Project

    This file is part of qTox, a Qt-based graphical interface for Tox.

    qTox is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    qTox is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with qTox.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef VIDEOFRAME_H
#define VIDEOFRAME_H

#include <QImage>
#include <QReadWriteLock>
#include <QRect>
#include <QSize>

extern "C"{
#include <libavcodec/avcodec.h>
}

#include <vpx/vpx_image.h>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief An ownernship and management class for AVFrames.
 *
 * VideoFrame takes ownership of an AVFrame* and allows fast conversions to other formats.
 * Ownership of all video frame buffers is kept by the VideoFrame, even after conversion. All
 * references to the frame data become invalid when the VideoFrame is deleted. We try to avoid
 * pixel format conversions as much as possible, at the cost of some memory.
 *
 * Every function in this class is thread safe apart from concurrent construction and deletion of
 * the object.
 *
 * This class uses the phrase "frame alignment" to specify the property that each frame's width is
 * equal to it's maximum linesize. Note: this is NOT "data alignment" which specifies how allocated
 * buffers are aligned in memory. Though internally the two are related, unless otherwise specified
 * all instances of the term "alignment" exposed from public functions refer to frame alignment.
 *
 * Frame alignment is an important concept because ToxAV does not support frames with linesizes not
 * directly equal to the width.
 */
class VideoFrame
{
public:
    /**
     * @brief Constructs a new instance of a VideoFrame, sourced by a given AVFrame pointer.
     * @param sourceFrame the source AVFrame pointer to use, must be valid.
     * @param dimensions the dimensions of the AVFrame, obtained from the AVFrame if not given.
     * @param pixFmt the pixel format of the AVFrame, obtained from the AVFrame if not given.
     * @param destructCallback callback function to run upon destruction of the VideoFrame
     * this callback is only run when destroying a valid VideoFrame (e.g. a VideoFrame instance in
     * which releaseFrame() was called upon it will not call the callback).
     * @param freeSourceFrame whether to free the source frame buffers or not.
     */
    VideoFrame(AVFrame* sourceFrame, QRect dimensions, int pixFmt, std::function<void()> destructCallback, bool freeSourceFrame = false);
    VideoFrame(AVFrame* sourceFrame, std::function<void()> destructCallback, bool freeSourceFrame = false);
    VideoFrame(AVFrame* sourceFrame, bool freeSourceFrame = false);

    /**
     * Destructor for VideoFrame.
     */
    ~VideoFrame();

    // Copy/Move operations are disabled for the VideoFrame, encapsulate with a std::shared_ptr to manage.

    VideoFrame(const VideoFrame& other) = delete;
    VideoFrame(VideoFrame&& other) = delete;

    const VideoFrame& operator=(const VideoFrame& other) = delete;
    const VideoFrame& operator=(VideoFrame&& other) = delete;

    /**
     * @brief Returns the validity of this VideoFrame.
     *
     * A VideoFrame is valid if it manages at least one AVFrame. A VideoFrame can be invalidated
     * by calling releaseFrame() on it.
     *
     * @return true if the VideoFrame is valid, false otherwise.
     */
    bool isValid();

    /**
     * @brief Retrieves an AVFrame derived from the source based on the given parameters.
     *
     * If a given frame does not exist, this function will perform appropriate conversions to
     * return a frame that fulfills the given parameters.
     *
     * @param frameSize the dimensions of the frame to get. If frame size is 0, defaults to source
     * frame size.
     * @param pixelFormat the desired pixel format of the frame.
     * @param requireAligned true if the returned frame must be frame aligned, false if not.
     * @return a pointer to a AVFrame with the given parameters or nullptr if the VideoFrame is no
     * longer valid.
     */
    const AVFrame* getAVFrame(QSize frameSize, const int pixelFormat, const bool requireAligned);

    /**
     * @brief Releases all frames managed by this VideoFrame and invalidates it.
     */
    void releaseFrame();

    /**
     * @brief Retrieves a copy of the source VideoFrame's dimensions.
     * @return QRect copy representing the source VideoFrame's dimensions.
     */
    inline QRect getSourceDimensions()
    {
        return sourceDimensions;
    }

    /**
     * @brief Retrieves a copy of the source VideoFormat's pixel format.
     * @return integer copy represetning the source VideoFrame's pixel format.
     */
    inline int getSourcePixelFormat()
    {
        return sourcePixelFormat;
    }

    /**
     * @brief Converts this VideoFrame to a QImage that shares this VideoFrame's buffer.
     *
     * The VideoFrame will be scaled into the RGB24 pixel format along with the given
     * dimension.
     *
     * @param frameSize the given frame size of QImage to generate. If frame size is 0, defaults to
     * source frame size.
     * @return a QImage that represents this VideoFrame, sharing it's buffers or a null image if
     * this VideoFrame is no longer valid.
     */
    QImage toQImage(QSize frameSize = {0, 0});

    /**
     * @brief Converts this VideoFrame to a vpx_image that shares this VideoFrame's buffer.
     *
     * Given that libvpx does not provide a way to create vpx_images that uses external buffers,
     * the vpx_image constructed by this function is done in a non-compliant way, requiring the
     * use of the C++ delete keyword to properly deallocate memory associated with this image.
     *
     * @param frameSize the given frame size of vpx_image to generate. If frame size is 0, defaults
     * to source frame size.
     * @return a vpx_image that represents this VideoFrame, sharing it's buffers or nullptr if this
     * VideoFrame is no longer valid.
     */
    vpx_image* toVpxImage(QSize frameSize = {0, 0});

    /**
     * @brief Data alignment parameter used to populate AVFrame buffers.
     *
     * This field is public in effort to standardized the data alignment parameter for all AVFrame
     * allocations.
     *
     * It's currently set to 32-byte alignment for AVX2 support.
     */
    static constexpr int dataAlignment = 32;

private:
    /**
     * @brief A class representing a structure that stores frame properties to be used as the key
     * value for a std::unordered_map.
     */
    class FrameBufferKey{
    public:
        /**
         * @brief Constructs a new FrameBufferKey with the given attributes.
         *
         * @param width the width of the frame.
         * @param height the height of the frame.
         * @param pixFmt the pixel format of the frame.
         * @param lineAligned whether the linesize matches the width of the image.
         */
        FrameBufferKey(const int width, const int height, const int pixFmt, const bool lineAligned);

        // Explictly state default constructor/destructor

        FrameBufferKey(const FrameBufferKey&) = default;
        FrameBufferKey(FrameBufferKey&&) = default;
        ~FrameBufferKey() = default;

        // Assignment operators are disabled for the FrameBufferKey

        const FrameBufferKey& operator=(const FrameBufferKey&) = delete;
        const FrameBufferKey& operator=(FrameBufferKey&&) = delete;

        /**
         * @brief Comparison operator for FrameBufferKey.
         *
         * @param other instance to compare against.
         * @return true if instances are equivilent, false otherwise.
         */
        inline bool operator==(const FrameBufferKey& other) const
        {
            return pixelFormat == other.pixelFormat &&
                   frameWidth == other.frameWidth &&
                   frameHeight == other.frameHeight &&
                   linesizeAligned == other.linesizeAligned;
        }

        /**
         * @brief Not equal to operator for FrameBufferKey.
         *
         * @param other instance to compare against
         * @return true if instances are not equivilent, false otherwise.
         */
        inline bool operator!=(const FrameBufferKey& other) const
        {
            return !operator==(other);
        }

        /**
         * @brief Hash function for class.
         *
         * This function computes a hash value for use with std::unordered_map.
         *
         * @param key the given instance to compute hash value of.
         * @return the hash of the given instance.
         */
        static inline size_t hash(const FrameBufferKey& key)
        {
            std::hash<int> intHasher;
            std::hash<bool> boolHasher;

            // Use java-style hash function to combine fields
            size_t ret = 47;

            ret = 37 * ret + intHasher(key.frameWidth);
            ret = 37 * ret + intHasher(key.frameHeight);
            ret = 37 * ret + intHasher(key.pixelFormat);
            ret = 37 * ret + boolHasher(key.linesizeAligned);

            return ret;
        }

    public:
        const int frameWidth;
        const int frameHeight;
        const int pixelFormat;
        const bool linesizeAligned;
    };

private:
    /**
     * @brief Generates a key object based on given parameters.
     *
     * @param frameSize the given size of the frame.
     * @param pixFmt the pixel format of the frame.
     * @param linesize the maximum linesize of the frame, may be larger than the width.
     * @return a FrameBufferKey object representing the key for the frameBuffer map.
     */
    static inline FrameBufferKey getFrameKey(const QSize& frameSize, const int pixFmt, const int linesize)
    {
        return getFrameKey(frameSize, pixFmt, frameSize.width() == linesize);
    }

    /**
     * @brief Generates a key object based on given parameters.
     *
     * @param frameSize the given size of the frame.
     * @param pixFmt the pixel format of the frame.
     * @param frameAligned true if the frame is aligned, false otherwise.
     * @return a FrameBufferKey object representing the key for the frameBuffer map.
     */
    static inline FrameBufferKey getFrameKey(const QSize& frameSize, const int pixFmt, const bool frameAligned)
    {
        return {frameSize.width(), frameSize.height(), pixFmt, frameAligned};
    }

    /**
     * @brief Retrieves an AVFrame derived from the source based on the given parameters without
     * obtaining a lock.
     *
     * This function is not thread-safe and must be called from a thread-safe context.
     *
     * Note: this function differs from getAVFrame() in that it returns a nullptr if no frame was
     * found.
     *
     * @param dimensions the dimensions of the frame.
     * @param pixelFormat the desired pixel format of the frame.
     * @param requireAligned true if the frame must be frame aligned, false otherwise.
     * @return a pointer to a AVFrame with the given parameters or nullptr if no such frame was
     * found.
     */
    AVFrame* retrieveAVFrame(const QSize& dimensions, const int pixelFormat, const bool requireAligned);

    /**
     * @brief Generates an AVFrame based on the given specifications.
     *
     * This function is not thread-safe and must be called from a thread-safe context.
     *
     * @param dimensions the required dimensions for the frame.
     * @param pixelFormat the required pixel format for the frame.
     * @param requireAligned true if the generated frame needs to be frame aligned, false otherwise.
     * @return an AVFrame with the given specifications.
     */
    AVFrame* generateAVFrame(const QSize& dimensions, const int pixelFormat, const bool requireAligned);

    /**
     * @brief Stores a given AVFrame within the frameBuffer map.
     *
     * This function is not thread-safe and must be called from a thread-safe context.
     *
     * @param frame the given frame to store.
     * @param dimensions the dimensions of the frame.
     * @param pixelFormat the pixel format of the frame.
     */
    void storeAVFrame(AVFrame* frame, const QSize& dimensions, const int pixelFormat);

    /**
     * @brief Releases all frames within the frame buffer.
     *
     * This function is not thread-safe and must be called from a thread-safe context.
     */
    void deleteFrameBuffer();

private:
    // Main framebuffer store
    std::unordered_map<FrameBufferKey, AVFrame*, std::function<decltype(FrameBufferKey::hash)>> frameBuffer {3, FrameBufferKey::hash};

    // Source frame
    const QRect sourceDimensions;
    const int sourcePixelFormat;
    const FrameBufferKey sourceFrameKey;
    const bool freeSourceFrame;

    // Destructor callback
    const std::function<void ()> destructCallback;

    // Concurrency
    QReadWriteLock frameLock {};
};

#endif // VIDEOFRAME_H
