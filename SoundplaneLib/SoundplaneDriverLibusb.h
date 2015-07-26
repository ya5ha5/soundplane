
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_DRIVER_LIBUSB__
#define __SOUNDPLANE_DRIVER_LIBUSB__

#include <condition_variable>
#include <mutex>
#include <thread>

#include <libusb.h>

#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"

class SoundplaneDriverLibusb : public SoundplaneDriver
{
public:
	/**
	 * listener may be nullptr
	 */
	SoundplaneDriverLibusb(SoundplaneDriverListener* listener);
	~SoundplaneDriverLibusb();

	void init();

	virtual int readSurface(float* pDest) override;
	virtual void flushOutputBuffer() override;
	virtual MLSoundplaneState getDeviceState() const override;
	virtual UInt16 getFirmwareVersion() const override;
	virtual std::string getSerialNumberString() const override;

	virtual const unsigned char *getCarriers() const override;
	virtual int setCarriers(const unsigned char *carriers) override;
	virtual int enableCarriers(unsigned long mask) override;

private:
	/**
	 * A RAII helper for libusb device handles. It closes the device handle on
	 * destruction.
	 */
	class LibusbDevice
	{
	public:
		LibusbDevice() {}

		/**
		 * Handle may be nullptr.
		 */
		explicit LibusbDevice(libusb_device_handle* handle) :
			mHandle(handle) {}

		LibusbDevice(const LibusbDevice &) = delete;
		LibusbDevice& operator=(const LibusbDevice &) = delete;

		LibusbDevice(LibusbDevice &&other)
		{
			*this = std::move(other);
		}

		LibusbDevice& operator=(LibusbDevice &&other)
		{
			std::swap(mHandle, other.mHandle);
			return *this;
		}

		~LibusbDevice()
		{
			if (mHandle)
			{
				libusb_close(mHandle);
			}
		}

		libusb_device_handle* get() const
		{
			return mHandle;
		}

	private:
		libusb_device_handle* mHandle = nullptr;
	};

	/**
	 * A RAII helper for claiming libusb device interfaces.
	 */
	class LibusbClaimedDevice
	{
	public:
		LibusbClaimedDevice() {}

		/**
		 * This constructor assumes ownership of an underlying LibusbDevice
		 * and attempts to claim the specified interface number. If that fails,
		 * the LibusbDevice is released and the created LibusbClaimedDevice
		 * is an empty one.
		 *
		 * handle may be an empty handle.
		 */
		LibusbClaimedDevice(LibusbDevice &&handle, int interfaceNumber) :
			mHandle(std::move(handle)),
			mInterfaceNumber(interfaceNumber) {
			// Attempt to claim the specified interface
			if (libusb_claim_interface(mHandle.get(), interfaceNumber) < 0) {
				// Claim failed. Reset underlying handle
				LibusbDevice empty;
				std::swap(empty, mHandle);
			}
		}

		~LibusbClaimedDevice() {
			if (mHandle.get() != nullptr) {
				libusb_release_interface(mHandle.get(), mInterfaceNumber);
			}
		}

		LibusbClaimedDevice(const LibusbDevice &) = delete;
		LibusbClaimedDevice& operator=(const LibusbDevice &) = delete;

		LibusbClaimedDevice(LibusbClaimedDevice &&other)
		{
			*this = std::move(other);
		}

		LibusbClaimedDevice& operator=(LibusbClaimedDevice &&other)
		{
			std::swap(mHandle, other.mHandle);
			std::swap(mInterfaceNumber, other.mInterfaceNumber);
			return *this;
		}

		libusb_device_handle* get() const
		{
			return mHandle.get();
		}

		explicit operator bool() const
		{
			return mHandle.get() != nullptr;
		}

	private:
		LibusbDevice mHandle;
		int mInterfaceNumber = 0;
	};

	/**
	 * Returns true if the process thread should quit.
	 *
	 * May spuriously wait for a shorter than the specified time.
	 */
	bool processThreadWait(int ms);
	/**
	 * Returns true if the process thread should quit.
	 */
	bool processThreadOpenDevice(LibusbClaimedDevice &outDevice);
	void processThreadCloseDevice();
	void processThread();

	bool mQuitting = false;
	std::mutex mMutex;  // Used with mCondition and protects mQuitting
	std::condition_variable mCondition;  // Used to wake up the process thread

	libusb_context				*mLibusbContext = nullptr;
	SoundplaneDriverListener	*mListener;
	unsigned char				mCurrentCarriers[kSoundplaneSensorWidth];

	std::thread					mProcessThread;

	std::atomic<MLSoundplaneState> mState;
};

#endif // __SOUNDPLANE_DRIVER_LIBUSB__
