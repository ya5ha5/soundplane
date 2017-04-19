
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TouchTracker.h"

#include <algorithm>

/*
std::ostream& operator<< (std::ostream& out, const Touch & t)
{
	out << std::setprecision(4);
	out << "[" << t.x << ", " << t.y << ", " << t.zf << " (" << t.age << ")]";
	return out;
}
*/

bool spansOverlap(Vec4 a, Vec4 b)
{
	return (within(b.x(), a.x(), a.y() ) || within(b.y(), a.x(), a.y()) ||
			((b.x() < a.x()) && (b.y() > a.y()))
			);
}

void replaceLastSpanInRow(std::array<Vec4, TouchTracker::kMaxSpansPerRow>& row, Vec4 b)
{
	 auto lastNonNull = std::find_if(row.rbegin(), row.rend(), [](Vec4 a){ return bool(a); });
	 if(lastNonNull != row.rend())
	 {
		*lastNonNull = b;
	 }
	 else
	 {
		 *(row.begin()) = b;
	 }
}

void appendSpanToRow(std::array<Vec4, TouchTracker::kMaxSpansPerRow>& row, Vec4 b)
{
	// if full (last element is not null), return
	if(row[TouchTracker::kMaxSpansPerRow - 1]) { debug() << "!"; return; }
	
	auto firstNull = std::find_if(row.begin(), row.end(), [](Vec4 a){ return !bool(a); });
	*firstNull = b;
}


void insertSpanIntoRow(std::array<Vec4, TouchTracker::kMaxSpansPerRow>& row, Vec4 b)
{
	// if full (last element is not null), return
	if(row[TouchTracker::kMaxSpansPerRow - 1]) { debug() << "!"; return; }
	
	for(int i=0; i<TouchTracker::kMaxSpansPerRow; i++)
	{
		Vec4 a = row[i];
		if(!a)
		{
			// empty, overwrite
			row[i] = b;
			return;
		}
		else if(within(b.x(), a.x(), a.y() ) || within(b.y(), a.x(), a.y() ) ||
				( (b.x() < a.x()) && (b.y() > a.y()) )
				)
		{
			// overlapping, combine
			// row[i] = Vec4(min(a.x(), b.x()), max(a.y(), b.y()), b.z(), b.w() );
			// overwrite
			row[i] = b;
			return;
		}
		else if(b.x() > a.y())
		{
			// past existing span, insert after
			for(int j = TouchTracker::kMaxSpansPerRow - 1; j > i; j--)
			{
				row[j] = row[j - 1];
			}
			row[i] = b;
			return;
		}
	}
	
}

void removeSpanFromRow(std::array<Vec4, TouchTracker::kMaxSpansPerRow>& row, int pos)
{
	int spans = row.size();
	// restore null at end in case needed 
	row[TouchTracker::kMaxSpansPerRow - 1] = Vec4::null();
	
	for(int j = pos; j < spans - 1; ++j)
	{
		Vec4 next = row[j + 1];
		row[j] = next;
		if(!next) break;
	}
}

Vec2 intersect(const LineSegment& a, const LineSegment& b)
{
	if((lengthIsZero(a)) || (lengthIsZero(b)))
	{
		return Vec2::null();
	}
	
	if( (a.start == b.start) || (a.start == b.end) || 
	   (a.end == b.start) || (a.end == b.end) )
	{
		return Vec2::null();
	}
	
	LineSegment a2 = translate(a, -a.start);
	LineSegment b2 = translate(b, -a.start);

	float aLen = length(a2);
	float cosTheta = a2.end.x()/aLen;
	float sinTheta = -(a2.end.y()/aLen);
	
	LineSegment b3 = multiply(Mat22(cosTheta, -sinTheta, sinTheta, cosTheta), b2);
	
	if(sign(b3.start.y()) == sign(b3.end.y())) return Vec2::null();
	
	float sectX = b3.end.x() + (b3.start.x() - b3.end.x())*b3.end.y()/(b3.end.y() - b3.start.y());
	
	if(!within(sectX, 0.f, aLen)) return Vec2::null();
	   	   
	return Vec2(a.start + Vec2(sectX*cosTheta, sectX*-sinTheta));
}

MLSignal gradX(MLSignal z)
{
	MLSignal r(z);
	int w = z.getWidth();
	int h = z.getHeight();
	for(int i=0; i<w; ++i)
	{
		for(int j=0; j<h; ++j)
		{
			float z1 = within(i - 1, 0, w) ? z(i - 1, j) : 0.f;
			float z2 = within(i + 1, 0, w) ? z(i + 1, j) : 0.f;
			r(i, j) = (z2 - z1)/2.f;
		}
	}
//	debug() << "GRAD X:" <<  r << "\n";
	return r;
}

MLSignal gradY(MLSignal z)
{
	MLSignal r(z);
	int w = z.getWidth();
	int h = z.getHeight();
	for(int i=0; i<w; ++i)
	{
		for(int j=0; j<h; ++j)
		{
			float z1 = within(j - 1, 0, h) ? z(i, j - 1) : 0.f;
			float z2 = within(j + 1, 0, h) ? z(i, j + 1) : 0.f;
			r(i, j) = (z2 - z1)/2.f;
		}
	}
	return r;
}

/*
Touch::Touch() : 
	x(0), y(0), z(0), dz(0), zf(0), zf10(0.), dzf(0.), 
	key(-1), age(0), retrig(0), releaseCtr(0)
{
}

Touch::Touch(float px, float py, float pz, float pdz) : 
	x(px), y(py), z(pz), dz(pdz), zf(0.), zf10(0.), dzf(0.), 
	key(-1), age(0), retrig(0), releaseCtr(0)
{
}
*/

TouchTracker::TouchTracker(int w, int h) :
	mWidth(w),
	mHeight(h),
	mpIn(0),
	mNumNewCentroids(0),
	mNumCurrentCentroids(0),
	mNumPreviousCentroids(0),
	mMatchDistance(2.0f),
	mNumPeaks(0),
	mOnThreshold(0.03f),
	mOffThreshold(0.02f),
	mTaxelsThresh(9),
	mQuantizeToKey(false),
	mTemplateThresh(0.003),
	mOverrideThresh(0.01),
	mCount(0),
	mMaxTouchesPerFrame(0),
	mBackgroundFilter(1, 1),
	mNeedsClear(true),
	mKeyboardType(rectangularA),
	mCalibrator(w, h),
	mSampleRate(1000.f),
	mBackgroundFilterFreq(0.125f),
	mPrevTouchForRotate(0),
	mRotate(false),
	mDoNormalize(true),
	mUseTestSignal(false)
{
//	mTouches.resize(kTrackerMaxTouches);	
//	mTouchesToSort.resize(kTrackerMaxTouches);	

	mTestSignal.setDims(w, h);
	mFitTestSignal.setDims(w, h);
	mGradientSignalX.setDims(w, h);
	mGradientSignalY.setDims(w, h);
	mTestSignal2.setDims(w, h);
	mCalibratedSignal.setDims(w, h);
	mCookedSignal.setDims(w, h);
	mBackground.setDims(w, h);
	mBackgroundFilterFrequency.setDims(w, h);
	mBackgroundFilterFrequency2.setDims(w, h);
	mFilteredInput.setDims(w, h);
	mSumOfTouches.setDims(w, h);
	mInhibitMask.setDims(w, h);
	mTemplateMask.setDims(w, h);
	mInputMinusBackground.setDims(w, h);
	mCalibrationProgressSignal.setDims(w, h);
	mResidual.setDims(w, h);
	mFilteredResidual.setDims(w, h);
	mTemp.setDims(w, h);
	mTempWithBorder.setDims(w + 2, h + 2);
	mRetrigTimer.setDims(w, h);
	mDzSignal.setDims(w, h);
	mBackgroundFilter.setDims(w, h);
	mBackgroundFilter.setSampleRate(mSampleRate);
	mTemplateScaled.setDims (kTemplateSize, kTemplateSize);


	
	// NEW
	int wb = bitsToContain(w);
	int hb = bitsToContain(h);
	
	// TODO complex signals as 2 z frames?
	mFFT1.setDims(1 << wb, 1 << hb);
	mFFT1i.setDims(1 << wb, 1 << hb);
	
	mFFT2.setDims(1 << wb, 1 << hb);
	mTouchFrequencyMask.setDims(1 << wb, 1 << hb);
	
	mTouchKernel.setDims(w, h);
	mTouchKerneli.setDims(w, h);
	
	makeFrequencyMask();
	
	// clear previous spans
	for(auto& row : mSpansHoriz1)
	{
		row.fill(Vec4::null());	
	}
	

}
		
TouchTracker::~TouchTracker()
{
}

void TouchTracker::setInputSignal(MLSignal* pIn)
{ 
	mpIn = pIn; 

}

void TouchTracker::setOutputSignal(MLSignal* pOut)
{ 
	mpOut = pOut; 
	int w = pOut->getWidth();
	int h = pOut->getHeight();
	
	if (w < 5)
	{
		debug() << "TouchTracker: output signal too narrow!\n";
		return;
	}
	if (h < mMaxTouchesPerFrame)
	{
		debug() << "error: TouchTracker: output signal too short to contain touches!\n";
		return;
	}
}

void TouchTracker::setMaxTouches(int t)
{
	int newT = clamp(t, 0, kTrackerMaxTouches);
	if(newT != mMaxTouchesPerFrame)
	{
		mMaxTouchesPerFrame = newT;
	}
}

// return the index of the key over the point p.
//
int TouchTracker::getKeyIndexAtPoint(const Vec2 p) 
{
	int k = -1;
	float x = p.x();
	float y = p.y();
	int ix, iy;
	switch (mKeyboardType)
	{
		case(rectangularA):
		default:
			MLRange xRange(3.5f, 59.5f);
			xRange.convertTo(MLRange(1.f, 29.f));
			float kx = xRange(x);
			kx = clamp(kx, 0.f, 29.f);
			ix = kx;
			
			MLRange yRange(1.25, 5.75);  // Soundplane A as measured
			yRange.convertTo(MLRange(1.f, 4.f));
			float ky = yRange(y);
			ky = clamp(ky, 0.f, 4.f);
			iy = ky;

			k = iy*30 + ix;
			break;
	}
	return k;
}

// return the center of the key over the point p.
//
Vec2 TouchTracker::getKeyCenterAtPoint(const Vec2 p) 
{
	float x = p.x();
	float y = p.y();
	int ix, iy;
	float fx, fy;
	switch (mKeyboardType)
	{
		case(rectangularA):
		default:
			MLRange xRange(3.5f, 59.5f);
			xRange.convertTo(MLRange(1.f, 29.f));
			float kx = xRange(x);
			kx = clamp(kx, 0.f, 30.f);
			ix = kx;
			
			MLRange yRange(1.25, 5.75);  // Soundplane A as measured
			yRange.convertTo(MLRange(1.f, 4.f));
			float ky = yRange(y);
			ky = clamp(ky, 0.f, 5.f);
			iy = ky;
			
			MLRange xRangeInv(1.f, 29.f);
			xRangeInv.convertTo(MLRange(3.5f, 59.5f));
			fx = xRangeInv(ix + 0.5f);
			
			MLRange yRangeInv(1.f, 4.f);
			yRangeInv.convertTo(MLRange(1.25, 5.75));
			fy = yRangeInv(iy + 0.5f);

			break;
	}
	return Vec2(fx, fy);
}

Vec2 TouchTracker::getKeyCenterByIndex(int idx) 
{
	// for Soundplane A only
	int iy = idx/30;
	int ix = idx - iy*30;
	
	MLRange xRangeInv(1.f, 29.f);
	xRangeInv.convertTo(MLRange(3.5f, 59.5f));
	float fx = xRangeInv(ix + 0.5f);

	MLRange yRangeInv(1.f, 4.f);
	yRangeInv.convertTo(MLRange(1.25, 5.75));
	float fy = yRangeInv(iy + 0.5f);

	return Vec2(fx, fy);
}

/*
int TouchTracker::touchOccupyingKey(int k)
{
	int r = -1;
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			if(t.key == k)
			{
				r = i;
				break;
			}
		}
	}
	return r;
}
*/

void TouchTracker::setRotate(bool b)
{ 
	mRotate = b; 
	if(!b)
	{
		mPrevTouchForRotate = 0;
	}
}

// add new touch at first free slot.  TODO touch allocation modes including rotate.
// return index of new touch.
//
/*
int TouchTracker::addTouch(const Touch& t)
{
	int newIdx = -1;
	int minIdx = 0;
	float minZ = 1.f;
	int offset = 0;
	
	if(mRotate)
	{
		offset = mPrevTouchForRotate;
		mPrevTouchForRotate++;
		if(mPrevTouchForRotate >= mMaxTouchesPerFrame)
		{
			mPrevTouchForRotate = 0;
		}
	}
	
	for(int jr=offset; jr<mMaxTouchesPerFrame + offset; ++jr)
	{
		int j = jr%mMaxTouchesPerFrame;

		Touch& r = mTouches[j];
		if (!r.isActive())
		{		
			r = t;
			r.age = 1;			
			r.releaseCtr = 0;			
			newIdx = j;
			return newIdx;
		}
		else
		{
			mPrevTouchForRotate++;
			float rz = r.z;
			if (r.z < minZ)
			{
				minIdx = j;
				minZ = rz; 
			}
		}
	}
	
	// if we got here, all touches were active.
	// replace the touch with lowest z if new touch is greater.
	//
	if (t.z > minZ)
	{
		int n = mTouches.size();		
		if (n > 0)
		{
			Touch& r = mTouches[minIdx];
			r = t;
			r.age = 1;	
			r.releaseCtr = 0;		
			newIdx = minIdx;
		}		
	}
	return newIdx;
}
*/

/*
// return index of touch at key k, if any.
//
int TouchTracker::getTouchIndexAtKey(const int k)
{
	int r = -1;
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{		
			if(t.key == k)
			{
				r = i;
				break;
			}
		}
	}
	return r;
}

// remove the touch at index by setting the age to 0.  leave the
// position intact for downstream filters etc.  
//
void TouchTracker::removeTouchAtIndex(int touchIdx)
{
	Touch& t = mTouches[touchIdx];
	t.age = 0;
	t.key = -1;
}
*/

void TouchTracker::clear()
{
	for (int i=0; i<kMaxTouches; i++)	
	{
		mTouches[i] = Vec4();	
		mTouches1[i] = Vec4();	
	}
	mBackgroundFilter.clear();
	mNeedsClear = true;
}


void TouchTracker::setThresh(float f) 
{ 
	const float kHysteresis = 0.001f;
	mOffThreshold = f; 
	mOnThreshold = mOffThreshold + kHysteresis; 
	mOverrideThresh = mOnThreshold*5.f;
	mCalibrator.setThreshold(mOnThreshold);
}

void TouchTracker::setLopass(float k)
{ 
	mLopass = k; 
}
			
// --------------------------------------------------------------------------------
#pragma mark main processing

Vec2 correctTouchPosition(const MLSignal& in, int ix, int iy)
{
	int width = in.getWidth();
	int height = in.getHeight();
	
	int x = clamp(ix, 1, width - 2);
	int y = clamp(iy, 1, height - 2);
	
	// over most of surface, return correctPeak()
	if(within(y, 2, height - 2))
	{
		return in.correctPeak(ix, iy, 0.75);
	}
	
	// else use tweaked correctPeak() for top, bottom rows (Soundplane A)
	Vec2 pos(x, y);
	float dx = (in(x + 1, y) - in(x - 1, y)) / 2.f;
	float dy = (in(x, y + 1) - in(x, y - 1)) / 2.f;
	float dxx = (in(x + 1, y) + in(x - 1, y) - 2*in(x, y));
	float dyy = (in(x, y + 1) + in(x, y - 1) - 2*in(x, y));
	float dxy = (in(x + 1, y + 1) + in(x - 1, y - 1) - in(x + 1, y - 1) - in(x - 1, y + 1)) / 4.f;
	if((dxx != 0.f)&&(dxx != 0.f)&&(dxx != 0.f))
	{
		float oneOverDiscriminant = 1.f/(dxx*dyy - dxy*dxy);
		float fx = (dyy*dx - dxy*dy) * oneOverDiscriminant;
		float fy = (dxx*dy - dxy*dx) * oneOverDiscriminant;
		
		fx = clamp(fx, -0.5f, 0.5f);
		fy = clamp(fy, -0.5f, 0.5f);
		
		if(y == 1)
		{
			if(fy > 0.)
				fy *= 2.f;
		}
		else if(y == height - 2)
		{
			if(fy < 0.)
				fy *= 2.f;
		}
		
		pos -= Vec2(fx, fy);
	}
	return pos;
}

/*
Vec3 TouchTracker::closestTouch(Vec2 pos)
{
	float minDist = MAXFLOAT;
	int minIdx = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			Vec2 p(t.x, t.y);
			p -= pos;
			float d = p.magnitude();
			if(d < minDist)
			{
				minDist = d;
				minIdx = i;
			}
		}
	}
	Touch& t = mTouches[minIdx];
	return(Vec3(t.x, t.y, t.z));
}

const float kInhibitRange = 8;
// currently, touches inhibit other new touches near them, below
// a conical slope away from the touch position. 
// return the greatest inhibit threshold of any other touch at the given position.		
float TouchTracker::getInhibitThreshold(Vec2 a)
{
	float maxInhibit = 0.f;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		const Touch& u = mTouches[i];
		if (u.isActive())
		{
			Vec2 b(u.x1, u.y1);		// use previous position		
			float dab = (a - b).magnitude();			
			// don't check against touch at input position (same touch)
			if(dab > 0.1f)
			{
				if(dab < kInhibitRange)
				{					
					float dru = 1.f - (dab / kInhibitRange);	
					dru = clamp(dru, 0.f, 1.f);
					float inhibitZ = u.z1*0.9*dru;
					maxInhibit = max(maxInhibit, inhibitZ);
				}
			}
		}
	}
	return maxInhibit;
}
*/

void TouchTracker::makeFrequencyMask()
{
	int h = mTouchFrequencyMask.getHeight();
	int w = mTouchFrequencyMask.getWidth();
	float c = 16;
	float sum = 0.f;
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{			
			// distance from 0 with wrap
			float px = min((float)i, (float)(w - i));
			float f;			
			if(px == 0)
			{
				// get rid of DC, which sharpens edges
				f = 0.;
			}
			else if(px < c)
			{
				// keep other frequencies below cutoff
				f = 1.f;
			}
			else
			{
				// get rid of high frequencies
				f = 0.;
			}
			mTouchFrequencyMask(i, j) = f;
			sum += f;
		}
	}

	// normalize
	sum /= h;
	mTouchFrequencyMask.scale((float)w/sum);
	mTouchFrequencyMask.dump(std::cout, true);
}

#pragma mark process
	
void TouchTracker::process(int)
{	
	if (!mpIn) return;
	const MLSignal& in(*mpIn);
	
	mFilteredInput.copy(in);
	
	// clear edges (should do earlier! TODO)
	int w = in.getWidth();
	int h = in.getHeight();
	for(int j=0; j<h; ++j)
	{
		mFilteredInput(0, j) = 0;
		mFilteredInput(w - 1, j) = 0;
	}
	
	if (mNeedsClear)
	{
		mBackground.copy(mFilteredInput);
		mBackgroundFilter.clear();
		mNeedsClear = false;
		return;
	}
		
	// filter out any negative values. negative values can shows up from capacitive coupling near edges,
	// from motion or bending of the whole instrument, 
	// from the elastic layer deforming and pushing up on the sensors near a touch. 
	bool doClamp = true;
	if(doClamp)
	{
		mFilteredInput.sigMax(0.f);
	}
	
	if (mCalibrator.isCalibrating())
	{		
		int done = mCalibrator.addSample(mFilteredInput);
		
		if(done == 1)
		{
			// Tell the listener we have a new calibration. We still do the calibration here in the Tracker, 
			// but the Model will be responsible for saving and restoring the calibration maps.
			if(mpListener)
			{
				mpListener->hasNewCalibration(mCalibrator.mCalibrateSignal, mCalibrator.mNormalizeMap, mCalibrator.mAvgDistance);
			}
		}
	}
	else
	{
		// TODO separate calibrator from Tracker and own in Model so test input can bypass calibration more cleanly
		bool doNormalize = false;
		if(doNormalize)
		{
			mCalibrator.normalizeInput(mFilteredInput);
		}
				
		// convolve with 3x3 smoothing kernel, twice.
		mFFT2 = mFilteredInput;
		float kc, kex, key, kk;			
//		kc = 16./32.; kex = 4./32.; key = 2./32.; kk=1./32.;	
		kc = 16./48.; kex = 8./48.; key = 4./48.; kk=2./48.;	
		mFFT2.convolve3x3xy(kc, kex, key, kk);
		mFFT2.convolve3x3xy(kc, kex, key, kk);
		
		mGradientSignalX = gradX(mFFT2);
		mGradientSignalY = gradY(mFFT2);
		
		mTestSignal = mFilteredInput;
		mTestSignal.scale(50.);
		
		mTestSignal2 = mFFT2;
		mTestSignal2.scale(50.);

		// MLTEST
		// mFilteredInput = mFFT2;
		mCalibratedSignal = mFFT2;
				

		if(mMaxTouchesPerFrame > 0)
		{
			HorizSpans intSpans = findSpansHoriz(mFFT2);
			mSpansHoriz = findZ2SpansHoriz(intSpans, mFFT2);
			
			findSpansVert();
			
			// ideas: time filter spans before correcting endpoints, or before combining.
			// limits on making / combining spans: support from z' or z needed 
			// use uglier correction of z to make spans. much less noisy though.
			
			// require bigger change in z'' than 0 crossing -- hysteresis over space?
			
		//	mSpansHoriz = combineSpansHoriz(mSpansHoriz);
			
			combineSpansVert();
			
		//	filterSpansHoriz();
			filterSpansVert();
			
		//	correctSpansHoriz(); // Soundplane A only. 
			// correctSpansVert(); // seems not useful for Soundplane A
			
			// copy filtered spans to output array
			{
				std::lock_guard<std::mutex> lock(mSpansHorizOutMutex);
				mSpansHorizOut = mSpansHoriz;
			}
			
			findPingsHoriz();
			findPingsVert();
			
			findLineSegmentsHoriz();
			findLineSegmentsVert();
			
			findIntersections();
			findTouches();
			
		//	matchTouches();
		}

		filterAndOutputTouches();
		
		{
			std::lock_guard<std::mutex> lock(mTouchesOutMutex);
			mTouchesOut = mTouches;
		}

	}
#if DEBUG   

	if (mCount++ > 1000) 
	{
		mCount = 0;
		//dumpTouches();
		
		debug() << "\n SPANS HORIZ: \n";
		int row = 0;
		for(auto rowArray : mSpansHorizOut)
		{
			debug() << "row " << row++ << ": ";
			for(Vec4 s : rowArray)
			{
				if(!s) 
				{
					break;	
				}
				else
				{
					bool isNull = (s == MLVec::kNullValue);
					
					debug() << isNull;
				}
				debug() << "[" << s.x() << "-" << s.y() << " " << s.z() << " " << s.w() << "] ";
			}
			debug() << "\n";
		}

		
		/*
		debug() << "\n SPANS VERT: \n";
		for(auto it = mSpansVert.begin(); it != mSpansVert.end(); ++it)
		{
			Vec3 s = *it;
			debug() << s.y() - s.x() << " ";
		}
		debug() << "\n INTERSECTIONS: " << mIntersections.size() << "\n";
		
		for(auto it = mIntersections.begin(); it != mIntersections.end(); ++it)
		{
			Vec3 s = *it;
			debug() << s.z() << " ";
		}
		 */
	}   
#endif  
}


// find spans over which z is higher than a small constant.
TouchTracker::HorizSpans TouchTracker::findSpansHoriz(const MLSignal& in)
{
	const float zThresh = 0.002;
	const float kMinSpanLength = 2.0f;
	
	int w = in.getWidth();
	int h = in.getHeight();
	HorizSpans out;
		
	for(int j=0; j<h; ++j)
	{
		out[j].fill(Vec4::null());	
		int spansInRow = 0;
		
		float spanStart, spanEnd;
		float z = 0.f;
		float zm1 = 0.f;
		spanStart = spanEnd = -1;
				
		for(int i=0; i < w; ++i)
		{
			z = in(i, j);
			if((z > zThresh) && (zm1 < zThresh)) 
			{
				// start span
				spanStart = i;
			}
			else if((spanStart >= 0) && ((z < zThresh) || (i == w - 1)) && (zm1 > zThresh)) 
			{
				// end span when z goes under thresh or active at end of sensor
				spanEnd = i;
				if(spanEnd - spanStart > kMinSpanLength)
				{
					if(spansInRow < kMaxSpansPerRow)
					{
						out[j][spansInRow++] = Vec4(spanStart, spanEnd, 0.f, 0.f); 		
					}
				}
				spanStart = spanEnd = -1;
			}
			zm1 = z;
		}
	}
	return out;
}


// for each horizontal span, find one or more subspans over which the 2nd derivative of z is negative.
TouchTracker::HorizSpans TouchTracker::findZ2SpansHoriz(const TouchTracker::HorizSpans& intSpans, const MLSignal& in)
{
	const float ddzThresh = 0.002f;
	const float kMinSpanLength = 1.f;
	
	int w = in.getWidth();
	
	HorizSpans y;
	int j = 0;
	for(auto row : intSpans)
	{
		y[j].fill(Vec4::null());

		for(auto intSpan : row)
		{
			if(!intSpan) break;
						
			float spanStart, spanEnd;
			float spanStartZ;
			float z = 0.f;
			float zm1 = 0.f;
			float dz = 0.f;
			float dzm1 = 0.f;
			float ddz = 0.f;
			float ddzm1 = 0.f;
			
			int left = intSpan.x();
			int right = intSpan.y();
			spanStart = spanEnd = -1;
			
			for(int i=left; i <= right; ++i)
			{
				z = in(i, j);				
				dz = z - zm1;
				ddz = dz - dzm1;
				
				if((ddz < -ddzThresh) && (ddzm1 > -ddzThresh))  // crossing negative thresh?
				{
	//				debug() << " [" << ddzm1 << "/" << ddz << "] ";
					
					// start span
					float m = ddz - ddzm1;	
					float xa = -ddz/m;
					spanStart = i - 1.f + xa; 	
					spanStartZ = z; 
				}
				else if((spanStart >= 0) && ((ddz > -ddzThresh) || (i == right)) && (ddzm1 < -ddzThresh)) 
				{
	//				debug() << " - [" << ddzm1 << "/" << ddz << "]\n";
					
					// end span if ddz goes back above 0 or the int span ends
					float m = ddz - ddzm1;				
					float xa = -ddz/m;
					spanEnd = i - 1.f + xa; 
					
					spanStart = clamp(spanStart, 1.f, w - 2.f);
					spanEnd = clamp(spanEnd, 1.f, w - 2.f);
					if(spanEnd - spanStart > kMinSpanLength)
					{
						appendSpanToRow(y[j], Vec4(spanStart, spanEnd, 0.f, 0.f));
					}
					spanStart = spanEnd = -1;
				}
				zm1 = z;
				dzm1 = dz;
				ddzm1 = ddz;
			}			
			
		}
		j++;
	}
	return y;
}

template<class InputIt, class ValueType = typename std::iterator_traits<InputIt>::value_type, class ShouldCombineFn, class CombineFn> 
void combineGroups(InputIt first, InputIt last, ShouldCombineFn shouldCombine, CombineFn combine)
{
	ValueType accum = *first;
	
	InputIt src = first;
	InputIt dest = first;
	for(src++; src != last; ++src)
	{
		ValueType next = *src;
		if(!next) break;
		
		if (shouldCombine(accum, next))
		{
			accum = combine(accum, next);
		}
		else
		{
			*dest++ = accum;
			accum = next;
		}
	}	
	
	*dest++ = accum;
	
	// null-object terminate
	if(dest != last)
	{
		*dest = ValueType::null();
	}
}

TouchTracker::HorizSpans TouchTracker::combineSpansHoriz(const TouchTracker::HorizSpans& inSpans)
{	
	const float combineDistance = 0.f;
	auto shouldCombineSpans = [&](Vec4 a, Vec4 b) -> bool { return (a.y() + combineDistance > b.x()) ; };
	auto combineSpans = [&](Vec4 a, Vec4 b){ return Vec4(a.x(), b.y(), 0.f, 0.f); }; // TODO variance
	
	TouchTracker::HorizSpans y;
	int j = 0;
	for(auto row : inSpans)
	{
		combineGroups(row.begin(), row.end(), shouldCombineSpans, combineSpans);
		y[j++] = row;
	}
	return y;
}



// TODO combine this and Horiz vrsion with a direction argument

// find vertical spans over which the 2nd derivative of z is negative,
// and during which the pressure exceeds mOnThreshold at some point.
void TouchTracker::findSpansVert()
{
	// some point on the span must be over this larger threshold to be recognized.
	const float zThresh = mOnThreshold/2;
	
	const float kMinSpanLength = 0.5f;
	
	const MLSignal& in = mFFT2;
	int w = in.getWidth();
	int h = in.getHeight();
	
	std::lock_guard<std::mutex> lock(mSpansVertMutex);
	mSpansVert.clear();
	
	// loop runs past column ends using zeros as input
	int top = h + 2;
	
	for(int i=0; i < w; ++i)
	{
		bool spanActive = false;
		bool spanExceedsThreshold = false;
		float spanStart, spanEnd;
		float z = 0.f;
		float zm1 = 0.f;
		float dz = 0.f;
		float dzm1 = 0.f;
		float ddz = 0.f;
		float ddzm1 = 0.f;
		bool pushSpan = false;
		
		for(int j=0; j<top; ++j)
		{
			pushSpan = false;
			zm1 = z;
			
			z = (within(j, 0, h)) ? in(i, j) : 0.f;
			
			dzm1 = dz;
			dz = z - zm1;
			
			ddzm1 = ddz;
			ddz = dz - dzm1;
			
			// near top there is not enough data for getting ddz, so relax criteria for start
			bool startNearTop = ((j >= h - 1) && (!spanActive) && (dzm1 > 0));
			
			if( ( (ddz < 0)&&(ddzm1 > 0) ) || (startNearTop) ) // start span
			{
				// get interpolated start
				float m = ddz - ddzm1;	
				float xa = -ddz/m;
				spanStart = j - 1.f + xa; 
				
				spanActive = true;
				if(z > zThresh) spanExceedsThreshold = true;
			}
			else if( (ddz > 0)&&(ddzm1 < 0) )
			{
				// get interpolated end
				float m = ddz - ddzm1;				
				float xa = -ddz/m;
				spanEnd = j - 1.f + xa; 
				
				if(spanExceedsThreshold)
				{
					pushSpan = true;
				}
				spanActive = false;
				spanExceedsThreshold = false;
			}
			else if(spanActive) 
			{
				if(z > zThresh) spanExceedsThreshold = true;
			}
			
			// close spans at column end (top)
			if(j == top - 1)
			{
				// end row
				if((spanActive && spanExceedsThreshold) && (ddz < 0))
				{
					// get interpolated end
					float m = ddz - ddzm1;				
					float xa = -ddz/m;
					spanEnd = j - 1.f + xa;				
					pushSpan = true;
				}				
			}
			
			if(pushSpan)
			{
				// DO allow span ends outside [0, h - 1]. 
				if(spanEnd - spanStart > kMinSpanLength)
				{
					mSpansVert.push_back(Vec3(spanStart, spanEnd, i)); 				
				}
			}
		}
	}
}




void TouchTracker::combineSpansVert()
{
	
}

void TouchTracker::filterSpansHoriz()
{

	// for each new span x0
		// is there a current span y1 over the same center ?	
		// if so, 	
			// combine new and current via IIR/variance filter -> current spans and reset decay count
		// else 
			// add new span to current spans and reset decay count
		
	// for each current span 
	// is there a new span over the same center?
	// if not, 
	// advance decay count
	// if decay count has expired
	// remove current span

	const float kP = 0.1f;
	
	HorizSpans y;

	// for each new span
//debug() << "combine:\n";
	for(int i = 0; i < kSensorRows; ++i)
	{
//		debug() << "row " << i << ":";
		
		std::array<Vec4, kMaxSpansPerRow> yRow;
		yRow.fill(Vec4::null());
		
		for(auto span : mSpansHoriz[i])
		{
			if(!span) break;
			
			float xStart = span.x();
			float xEnd = span.y();
			float xCenter = (xStart + xEnd)/2.f; 
			
			Vec4 combinedSpan;
			
			// is there a current span overlapping ?
			auto overlap = [&](Vec4 &a){ 
				return ( (a.x() < xCenter) && (a.y() > xCenter) ); };		
			auto itRowA = std::find_if(mSpansHoriz1[i].begin(), mSpansHoriz1[i].end(), overlap); 

			// if so, combine new and current spans and get running variance
			
			if(itRowA != mSpansHoriz1[i].end())
			{
				// filter start / end
				Vec4 span1 = *itRowA;
				combinedSpan.setX(span1.x()*(1.0f - kP) + span.x()*kP);
				combinedSpan.setY(span1.y()*(1.0f - kP) + span.y()*kP);
				
	//			debug() << "C";

				// get variance
			}
			else
			{
				// insert new span to current spans 
				combinedSpan = span;
			//	mSpansHoriz1[i][0] = span;
	//			debug() << "+";
		//		debug() << "new: " << i << "\n";
			}	
			
			appendSpanToRow(yRow, combinedSpan);
		}
		
		for(auto span1 : mSpansHoriz1[i])
		{
			if(!span1) break;
			
			float xStart = span1.x();
			float xEnd = span1.y();
			float xCenter = (xStart + xEnd)/2.f; 
			
			Vec4 combinedSpan;

			// is there a new span over the same center ?
			auto overlap = [&](Vec4 &a){ 
				return ( (a.x() < xCenter) && (a.y() > xCenter) ); };		
			auto itRowA = std::find_if(mSpansHoriz[i].begin(), mSpansHoriz[i].end(), overlap); 
			
			// if not, decay
			if(itRowA == mSpansHoriz[i].end())
			{
		//		debug() << "D";
				
				float age = span1.z() + 1.f;
			
				   
				   if(age < 100.f)
				   {
					   combinedSpan = Vec4(xStart, xEnd, age, 0.f);
					   insertSpanIntoRow(yRow, combinedSpan);
				   }
				else
				{
	//				debug() << "D @ " << age << "\n";;
				}
			}
			else
			{
	//			debug() << ".";
	//			combinedSpan = span1;
	//			insertSpanIntoRow(yRow, combinedSpan);
				
			}
		}
		
		y[i] = yRow;
//		debug() << "\n";
	}
//	debug() << "\n";

		
	// copy accumulator to output
	mSpansHoriz = y;
	mSpansHoriz1 = y;

}

void TouchTracker::filterSpansVert()
{
	
}

// piecewise triangle wave fn with period 2
float triangle(float x)
{
	float m = x - 0.5f;
	float t = m - floor(m);
	float a = ((0.5f*x - floor(0.5f*x)) > 0.5) ? -1.f : 1.f;
	float u = fabs(2.0f*(t - 0.5f));
	return u*a;
}

void TouchTracker::correctSpansHoriz()
{
	// Soundplane A - specific correction for key mechanicals
	// specific to smoothing kernel
	for(auto& row : mSpansHoriz)
	{
		for(auto& span : row)
		{
			if(!span) break;
			
			float a = span.x(); // start
			float b = span.y(); // end
			float c = (a + b)/2.f;
			
			float k = 0.2f; // by inspection //-mSpanCorrect;
			// k = -mSpanCorrect;
			
			// can be lookup table for optimized version
			float ra = k*triangle(c); 
			float rb = k*triangle(c); 
			
			span = Vec4(a + ra, b - rb, span.z(), span.w());
		}	
	}
}

void TouchTracker::correctSpansVert()
{
	// Soundplane A - specific correction for key mechanicals
	// specific to smoothing kernel
	for(auto it = mSpansVert.begin(); it != mSpansVert.end(); ++it)
	{
		Vec3 s = *it;
		float a = s.x(); // y1 
		float b = s.y(); // not really y, rather y2
		int col = s.z();
		float c = (a + b)/2.f;
		
		float k = -mSpanCorrect;
		
		// can be lookup table for optimized version
		float ra = k*triangle(c); 
		float rb = k*triangle(c); 
		
		*it = Vec3(a + ra, b - rb, col);
	}	
}


void TouchTracker::findPingsHoriz()
{
	const float k1 = 3.5f;
	const float fingerWidth = k1 / 2.f;

	const MLSignal& in = mFilteredInput;//mFFT2;
	std::lock_guard<std::mutex> lock(mPingsHorizMutex);
	mPingsHoriz.clear();
	
	int j = 0;
	for(auto& row : mSpansHoriz)
	{
		for(auto& s : row)
		{
			float pingX, pingZ;
			
			float a = s.x(); // start 
			float b = s.y(); // end
			
			float d = b - a;
			if(d < k1)
			{
				pingX = (a + b)/2.f;
				pingZ = in.getInterpolatedLinear(pingX, j);
				mPingsHoriz.push_back(Vec3(pingX, j, pingZ));			
			}
			else
			{
				pingX = a + fingerWidth;
				pingZ = in.getInterpolatedLinear(pingX, j);
				mPingsHoriz.push_back(Vec3(pingX, j, pingZ));			
				
				pingX = b - fingerWidth;
				pingZ = in.getInterpolatedLinear(pingX, j);
				mPingsHoriz.push_back(Vec3(pingX, j, pingZ));			
			}
		}
		j++;
	}
}

void TouchTracker::findPingsVert()
{
	const float k1 = 3.0f; // by inspection. could use calibration.
	const float fingerHeight = k1 / 2.f;
	
	const MLSignal& in = mFFT2;
	std::lock_guard<std::mutex> lock(mPingsVertMutex);
	mPingsVert.clear();
	
	// TODO add tweak for top + bottom
	
	for(auto it = mSpansVert.begin(); it != mSpansVert.end(); ++it)
	{
		float pingY, pingZ;
		
		Vec3 s = *it;
		float a = s.x(); // y1
		float b = s.y(); // y2
		int col = s.z();
		
		float d = b - a;
		if(d < k1) // threshold could be location-variant based on calibration 
		{
			pingY = (a + b)/2.f;
			pingZ = in.getInterpolatedLinear(col, pingY);
			mPingsVert.push_back(Vec3(col, pingY, pingZ));			
		}
		else
		{
			pingY = a + fingerHeight;
			pingZ = in.getInterpolatedLinear(col, pingY);
			mPingsVert.push_back(Vec3(col, pingY, pingZ));			
			
			pingY = b - fingerHeight;
			pingZ = in.getInterpolatedLinear(col, pingY);
			mPingsVert.push_back(Vec3(col, pingY, pingZ));			
		}
	}
}

void TouchTracker::findLineSegmentsHoriz()
{
	std::lock_guard<std::mutex> lock(mSegmentsHorizMutex);
	mSegmentsHoriz.clear();
	
	const MLSignal& in = mFFT2; // unused but for dims, fix
	int w = in.getWidth();
	int h = in.getHeight();
	
	// declarative:
	//
	// for each row i
	// for each ping a in row i
	// b = closest ping in row(i + 1) to a
	// if fabs(a.x() - b.x()) < kMaxDist
	// make a line segment from a to b
	
	
	// functional:
	//
	// pingsA = pingsInRow(i);
	// pingsB = pingsInRow(i + 1);
	// pingsBClosestToA = map(pingsA, [](ping p){ findClosest(p, pingsB); };
	// makeLineSegments(reduce(makePairs(pingsA, pingsBClosestToA), [](pair<pings> r){ fabs(r.first().x() - r.second().x()) < kMaxDist; };
	
	
	// bespoke: known ordered data
	//
	// for each row i
	// iteratorA = start of row i
	// iteratorB = start of row i + 1
	// while not (done(iteratorA) || done(iteratorB))
	// if fabs(a.x() - b.x()) < kMaxDist
	// makeLineSegment(a, b)
	// if a.x() < b.x()
	// advance iteratorA
	// else
	// advance iteratorB
	
	
	//	debug() << std::setprecision(4);
	const float kMaxDist = 1.0f;

	for(int i=0; i<h - 1; ++i)
	{
		//	debug() << "ROW " << i << "\n";
		auto itRowA = std::find_if(mPingsHoriz.begin(), mPingsHoriz.end(), [&](Vec3 &v){ return v.y() == i; }); 
		auto itRowB = std::find_if(mPingsHoriz.begin(), mPingsHoriz.end(), [&](Vec3 &v){ return v.y() == i + 1; }); 
		
		while( 
			  (itRowA != mPingsHoriz.end()) && (itRowB != mPingsHoriz.end())			  
			  && (itRowA->y() == i) && (itRowB->y() == i + 1) )
		{
			//			debug() << "[ " << *itRowA << *itRowB << " ] ";
			float ax = itRowA->x();
			float bx = itRowB->x();
			float ab = bx - ax;
			if(fabs(ab) < kMaxDist)
			{
				mSegmentsHoriz.push_back(Vec3(ax, bx, i));
				//			debug() << Vec3(ax, bx, i);
				itRowA++;
				itRowB++;
			}
			else
			{
			if(ab > 0)
			{
				itRowA++;
			}
			else
			{
				itRowB++;
			}
			}
		}
		//	debug() << "\n";
	}
}

void TouchTracker::findLineSegmentsVert()
{
	std::lock_guard<std::mutex> lock(mSegmentsVertMutex);
	mSegmentsVert.clear();
	

	const MLSignal& in = mFFT2; // unused but for dims, fix
	int w = in.getWidth();
	int h = in.getHeight();
	
	const float kMaxDist = 0.25f;
	for(int i=0; i<w - 1; ++i)
	{
		auto itColA = std::find_if(mPingsVert.begin(), mPingsVert.end(), [&](Vec3 &v){ return v.x() == i; }); 
		auto itColB = std::find_if(mPingsVert.begin(), mPingsVert.end(), [&](Vec3 &v){ return v.x() == i + 1; }); 
		
		while( 
			  (itColA != mPingsVert.end()) && (itColB != mPingsVert.end())			  
			  && (itColA->x() == i) && (itColB->x() == i + 1) )
		{
			float ay = itColA->y();
			float by = itColB->y();
			float ab = by - ay;
			if(fabs(ab) < kMaxDist)
			{
				mSegmentsVert.push_back(Vec3(ay, by, i));
				itColA++;
				itColB++;
			}
			else
			{
				if(ab > 0)
				{
					itColA++;
				}
				else
				{
					itColB++;
				}
			}
		}
	}
}


void TouchTracker::findIntersections()
{
	// TODO all these are just needed for the debug printing and can go away
	std::lock_guard<std::mutex> lock(mIntersectionsMutex);
	mIntersections.clear();
	
	for(auto itH = mSegmentsHoriz.begin(); itH != mSegmentsHoriz.end(); ++itH)
	{
		float ax1 = itH->x();
		float ax2 = itH->y();
		float ay1 = itH->z();
		float ay2 = ay1 + 1.f;
		
		// mayIntersect: does the column of vert segment v contain one of our x coordinate ends?
		auto mayIntersect = [&](Vec3 &v)
			{ return within(ax1, v.z(), v.z() + 1) || within(ax2, v.z(), v.z() + 1); };
		
		auto itV = std::find_if(mSegmentsVert.begin(), mSegmentsVert.end(), mayIntersect); 
		
		// segments vert are already ordered by row, so having found one we can just iterate through them
		while( (itV != mSegmentsVert.end()) && mayIntersect(*itV) )
		{
			//debug() << "(" << ax1 << ", " << ax2 << ")" << *itV;
			
			// todo make segments vec of LineSegments
			float bx1 = itV->z();
			float bx2 = itV->z() + 1.f;
			float by1 = itV->x();
			float by2 = itV->y();
			
			Vec2 w = intersect(LineSegment(Vec2(ax1, ay1), Vec2(ax2, ay2)), LineSegment(Vec2(bx1, by1), Vec2(bx2, by2)));
			float z = mFilteredInput.getInterpolatedLinear(w);
			
			if(w)
			{
				mIntersections.push_back(Vec4(w.x(), w.y(), z, 0));
			}
			
			itV++;
		}
	}
}

void TouchTracker::findTouches()
{
	// cluster intersections to get touches. 
	// light intersections cluster more easily. 
	// diagonals of dig. pairs need work
	
	mTouches.fill(Vec4()); 
	
	int currentGroup = 1;
	int n = mIntersections.size();
	
	// sort by z
	std::sort(mIntersections.begin(), mIntersections.end(), [](Vec4 va, Vec4 vb){return va.z() > vb.z();});
	
	for(int i=0; i<n; ++i)
	{
		Vec4 a = mIntersections[i];
		
		// get radius we can move this touch to combine with others		
		const float xDist = 0.01f;//1.5f;
		const float yDist = 0.01f;//1.5f;
		
		// TODO increase radius for light touches?
		float distScale = 1.0f;
		
		Vec4 c;
		float maxZ = 0.f;
		bool found = false;
		int foundIdx = -1;
		
		// get blob of maximum z within radius maxDist
		for(int j=0; j<n; ++j)
		{
			if(j != i)
			{
				Vec4 b = mIntersections[j];
				Vec2 ab = a.xy() - b.xy();
				ab.setX(ab.x()/(xDist*distScale)); 
				ab.setY(ab.y()/(yDist*distScale)); 
				float dist = ab.magnitude(); // TODO magnitude(ab);
				if(dist < 1.f)
				{
					if(b.z() > maxZ)
					{
						found = true;
						foundIdx = j;
						maxZ = b.z();
					}
				}
			}
		}

		// set groups
		if((found) && (maxZ > a.z()))
		{
			int groupOfB = mIntersections[foundIdx].w();
			if(!groupOfB)
			{
				mIntersections[foundIdx].setW(currentGroup++);
			}
			mIntersections[i].setW(mIntersections[foundIdx].w());
		}
		else
		{
			mIntersections[i].setW(currentGroup++);
		}
	}
	
	// collect groups into new touches
	int touches = 0;
	for(int g=1; g<currentGroup; ++g)
	{
		Vec4 centroid;
		for(int i=0; i<n; ++i)
		{
			if(mIntersections[i].w() == g)
			{
				Vec3 a = mIntersections[i];
				a.setX(a.x()*a.z());
				a.setY(a.y()*a.z());
				centroid += a;
			}
		}
		float tz = centroid.z();
		centroid /= tz;
		
		float zFromInput = mFFT2.getInterpolatedLinear(Vec2(centroid.x(), centroid.y()));
		
		centroid.setZ(zFromInput);
		centroid.setW(1); // age
		
		if(zFromInput > mOnThreshold)
		{
			mTouches[touches++] = centroid;
			if(touches >= mMaxTouchesPerFrame) break;
		}
	}
	// zero-terminate
	if(touches < kMaxTouches)
	{
		mTouches[touches] = Vec4();
	}
}

void TouchTracker::matchTouches()
{		
	// sort by age so that newest touches will be matched last
	std::sort(mTouches.begin(), mTouches.end(), [](Vec4 a, Vec4 b){return a.w() > b.w();});

	// match mTouches with previous touches in mTouches1 and put results in workingTouches
	std::array<Vec4, kMaxTouches> workingTouches;	
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Vec4 u = mTouches[i];
		if(u != Vec4()) 
		{
			float minDist = MAXFLOAT;
			int minIdx = 0;
			
			for(int j=0; j<mMaxTouchesPerFrame; ++j)
			{
				Vec4 v = mTouches1[j];
				
				if(workingTouches[j] == Vec4())
				{
					Vec2 u2(u.x(), u.y());
					Vec2 v2(v.x(), v.y());					
					float uvDist = (u2 - v2).magnitude();
					
					if(uvDist < minDist)
					{
						minDist = uvDist;
						minIdx = j;
					}		
				}
			}
			workingTouches[minIdx] = u;			 
		}
	}
	
	// copy / merge all touches from workingTouches to mTouches, including empty ones
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		// increment ages
		Vec4 u = workingTouches[i];
		Vec4 v = mTouches[i];
		int age = u.w();
		int age1 = v.w();
		int newAge = 0;
		if(age)
		{
			newAge = age1 + 1;
		}
		else
		{
			newAge = 0;
		}
		mTouches[i] = Vec4(u.x(), u.y(), u.z(), newAge);
	}
	
	// store history
	mTouches1 = mTouches;
}

void TouchTracker::filterAndOutputTouches()
{
	// filter touches
	// filter x and y for output
	// filter touches and write touch data to one frame of output signal.
	//
	MLSignal& out = *mpOut;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = mTouches[i];
		
		out(xColumn, i) = t.x();
		out(yColumn, i) = t.y();
		out(zColumn, i) = t.z() - mOnThreshold;
		out(ageColumn, i) = t.w();
	}
}

void TouchTracker::setDefaultNormalizeMap()
{
	mCalibrator.setDefaultNormalizeMap();
	mpListener->hasNewCalibration(mNullSig, mNullSig, -1.f);
}

// --------------------------------------------------------------------------------
#pragma mark calibration


TouchTracker::Calibrator::Calibrator(int w, int h) :
	mActive(false),
	mHasCalibration(false),
	mHasNormalizeMap(false),
	mCollectingNormalizeMap(false),
	mSrcWidth(w),
	mSrcHeight(h),
	mWidth(w),
	mHeight(h),
	mAutoThresh(0.05f)
{
	// resize sums vector and signals in it
	mData.resize(mWidth*mHeight);//*kCalibrateResolution*kCalibrateResolution);
	mDataSum.resize(mWidth*mHeight);//*kCalibrateResolution*kCalibrateResolution);
	mSampleCount.resize(mWidth*mHeight);
	mPassesCount.resize(mWidth*mHeight);
	for(int i=0; i<mWidth*mHeight; ++i)
	{
		mData[i].setDims(kTemplateSize, kTemplateSize);
		mData[i].clear();
		mDataSum[i].setDims(kTemplateSize, kTemplateSize);
		mDataSum[i].clear();
		mSampleCount[i] = 0;
		mPassesCount[i] = 0;
	}
	mIncomingSample.setDims(kTemplateSize, kTemplateSize);
	mVisSignal.setDims(mWidth, mHeight);
	mNormalizeMap.setDims(mSrcWidth, mSrcHeight);
	
	mNormalizeCount.setDims(mSrcWidth, mSrcHeight);
	mFilteredInput.setDims(mSrcWidth, mSrcHeight);
	mTemp.setDims(mSrcWidth, mSrcHeight);
	mTemp2.setDims(mSrcWidth, mSrcHeight);

	makeDefaultTemplate();
}

TouchTracker::Calibrator::~Calibrator()
{
}

void TouchTracker::Calibrator::begin()
{
	MLConsole() << "\n****************************************************************\n\n";
	MLConsole() << "Hello and welcome to tracker calibration. \n";
	MLConsole() << "Collecting silence, please don't touch.";
	
	mFilteredInput.clear();
	mSampleCount.clear();
	mPassesCount.clear();
	mVisSignal.clear();
	mNormalizeMap.clear();
	mNormalizeCount.clear();
	mTotalSamples = 0;
	mStartupSum = 0.;
	for(int i=0; i<mWidth*mHeight; ++i)
	{
		float maxSample = 1.f;
		mData[i].fill(maxSample);
		mDataSum[i].clear(); 
		mSampleCount[i] = 0;
		mPassesCount[i] = 0;
	}
	mPeak = Vec2();
	age = 0;
	mActive = true;
	mHasCalibration = false;
	mHasNormalizeMap = false;
	mCollectingNormalizeMap = false;
}

void TouchTracker::Calibrator::cancel()
{
	if(isCalibrating())
	{
		mActive = false;
		MLConsole() << "\nCalibration cancelled.\n";
	}
}

void TouchTracker::Calibrator::setDefaultNormalizeMap()
{
	mActive = false;
	mHasCalibration = false;
	mHasNormalizeMap = false;
}

void TouchTracker::Calibrator::makeDefaultTemplate()
{
	mDefaultTemplate.setDims (kTemplateSize, kTemplateSize);
		
	int w = mDefaultTemplate.getWidth();
	int h = mDefaultTemplate.getHeight();
	Vec2 vcenter(h/2.f, w/2.f);
	
	// default scale -- not important because we want to calibrate.
	Vec2 vscale(3.5, 3.);	 
	
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			Vec2 vdistance = Vec2(i + 0.5f, j + 0.5f) - vcenter;
			vdistance /= vscale;
			float d = clamp(vdistance.magnitude(), 0.f, 1.f);
			mDefaultTemplate(i, j) = 1.0*(1.0f - d);
		}
	}
}

// get the template touch at the point p by bilinear interpolation
// from the four surrounding templates.
const MLSignal& TouchTracker::Calibrator::getTemplate(Vec2 p) const
{
	static MLSignal temp1(kTemplateSize, kTemplateSize);
	static MLSignal temp2(kTemplateSize, kTemplateSize);
	static MLSignal d00(kTemplateSize, kTemplateSize);
	static MLSignal d10(kTemplateSize, kTemplateSize);
	static MLSignal d01(kTemplateSize, kTemplateSize);
	static MLSignal d11(kTemplateSize, kTemplateSize);
	if(mHasCalibration)
	{
		Vec2 pos = getBinPosition(p);
		Vec2 iPos, fPos;
		pos.getIntAndFracParts(iPos, fPos);	
		int idx00 = iPos.y()*mWidth + iPos.x();


		d00.copy(mCalibrateSignal.getFrame(idx00));
        if(iPos.x() < mWidth - 3)
        {
            d10.copy(mCalibrateSignal.getFrame(idx00 + 1));
        }
        else
        {
            d10.copy(mDefaultTemplate);
        }
        
        if(iPos.y() < mHeight - 1)
        {
            d01.copy(mCalibrateSignal.getFrame(idx00 + mWidth));
        }
        else
        {
            d01.copy(mDefaultTemplate);
        }
        
        if ((iPos.x() < mWidth - 3) && (iPos.y() < mHeight - 1))
        {
            d11.copy(mCalibrateSignal.getFrame(idx00 + mWidth + 1));
        }
        else
        {
            d11.copy(mDefaultTemplate);
        }
		temp1.copy(d00);
        
        temp1.sigLerp(d10, fPos.x());
		temp2.copy(d01);
		temp2.sigLerp(d11, fPos.x());
		temp1.sigLerp(temp2, fPos.y());

		return temp1;
	}
	else
	{
		return mDefaultTemplate;
	}
}

Vec2 TouchTracker::Calibrator::getBinPosition(Vec2 pIn) const
{
	// Soundplane A
	static MLRange binRangeX(2.0, 61.0, 0., mWidth);
	static MLRange binRangeY(0.5, 6.5, 0., mHeight);
	Vec2 minPos(2.5, 0.5);
	Vec2 maxPos(mWidth - 2.5f, mHeight - 0.5f);
	Vec2 pos(binRangeX(pIn.x()), binRangeY(pIn.y()));
	return vclamp(pos, minPos, maxPos);
}

void TouchTracker::Calibrator::normalizeInput(MLSignal& in)
{
	if(mHasNormalizeMap)
	{
		in.multiply(mNormalizeMap);
	}
}

// return true if the current cell (i, j) is used by the current stage of calibration
bool TouchTracker::Calibrator::isWithinCalibrateArea(int i, int j)
{
	if(mCollectingNormalizeMap)
	{
		return(within(i, 1, mWidth - 1) && within(j, 0, mHeight));
	}
	else
	{
		return(within(i, 2, mWidth - 2) && within(j, 0, mHeight));
	}
}

float TouchTracker::Calibrator::makeNormalizeMap()
{			
	int samples = 0;
	float sum = 0.f;
	for(int j=0; j<mSrcHeight; ++j)
	{
		for(int i=0; i<mSrcWidth; ++i)
		{			
			if(isWithinCalibrateArea(i, j))
			{
				float sampleSum = mNormalizeMap(i, j);
				float sampleCount = mNormalizeCount(i, j);
				float sampleAvg = sampleSum / sampleCount;
				mNormalizeMap(i, j) = 1.f / sampleAvg;
				sum += sampleAvg;
				samples++;
			}
			else
			{
				mNormalizeMap(i, j) = 0.f;
			}
		}
	}
    
	float mean = sum/(float)samples;
	mNormalizeMap.scale(mean);	
	
	// TODO analyze normal map! edges look BAD
	// MLTEST
	// constrain output values
	mNormalizeMap.sigMin(3.f);
	mNormalizeMap.sigMax(0.125f);	
	
	// return maximum
	Vec3 vmax = mNormalizeMap.findPeak();
	float rmax = vmax.z();
	
	mHasNormalizeMap = true;
	return rmax;
}

void TouchTracker::Calibrator::getAverageTemplateDistance()
{
	MLSignal temp(mWidth, mHeight);
	MLSignal tempSample(kTemplateSize, kTemplateSize);
	float sum = 0.f;
	int samples = 0;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{						
			int idx = j*mWidth + i;
			
			// put mean of input samples into temp signal at i, j
			temp.clear();
			tempSample.copy(mDataSum[idx]);
			tempSample.scale(1.f / (float)mSampleCount[idx]);
			temp.add2D(tempSample, i - kTemplateRadius, j - kTemplateRadius);
			
			float diff = differenceFromTemplateTouch(temp, Vec2(i, j));
			sum += diff;
			samples++;
			// debug() << "template diff [" << i << ", " << j << "] : " << diff << "\n";	
		}
	}
	mAvgDistance = sum / (float)samples;
}

// input: the pressure data, after static calibration (tare) but otherwise raw.
// input feeds a state machine that first collects a normalization map, then
// collects a touch shape, or kernel, at each point. 
int TouchTracker::Calibrator::addSample(const MLSignal& m)
{
	int r = 0;
	static Vec2 intPeak1;
	
	static MLSignal f2(mSrcWidth, mSrcHeight);
	static MLSignal input(mSrcWidth, mSrcHeight);
	static MLSignal tare(mSrcWidth, mSrcHeight);
	static MLSignal normTemp(mSrcWidth, mSrcHeight);
    
    // decreasing this will collect a wider area during normalization,
    // smoothing the results.
    const float kNormalizeThreshold = 0.125f;
	
	float kc, ke, kk;
	kc = 4./16.; ke = 2./16.; kk=1./16.;
	
	// simple lopass time filter for calibration
	f2 = m;
	f2.subtract(mFilteredInput);
	f2.scale(0.1f);
	mFilteredInput.add(f2);
	input = mFilteredInput;
	input.sigMax(0.);		
		
	// get peak of sample data
	Vec3 testPeak = input.findPeak();	
	float peakZ = testPeak.z();
	
	const int startupSamples = 1000;
	const int waitAfterNormalize = 2000;
	if (mTotalSamples < startupSamples)
	{
		age = 0;
		mStartupSum += peakZ;
		if(mTotalSamples % 100 == 0)
		{
			MLConsole() << ".";
		}
	}
	else if (mTotalSamples == startupSamples)
	{
		age = 0;
		//mAutoThresh = kNormalizeThresh;
		mAutoThresh = mStartupSum / (float)startupSamples * 10.f;	
		MLConsole() << "\n****************************************************************\n\n";
		MLConsole() << "OK, done collecting silence (auto threshold: " << mAutoThresh << "). \n";
		MLConsole() << "Now please slide your palm across the surface,  \n";
		MLConsole() << "applying a firm and even pressure, until all the rectangles \n";
		MLConsole() << "at left turn blue.  \n\n";
		
		mNormalizeMap.clear();
		mNormalizeCount.clear();
		mCollectingNormalizeMap = true;
	}		
	else if (mCollectingNormalizeMap)
	{
		// smooth temp signal, duplicating values at border
		normTemp.copy(input);		
		normTemp.convolve3x3rb(kc, ke, kk);
		normTemp.convolve3x3rb(kc, ke, kk);
		normTemp.convolve3x3rb(kc, ke, kk);
	
		if(peakZ > mAutoThresh)
		{
			// collect additions in temp signals
			mTemp.clear(); // adds to map
			mTemp2.clear(); // adds to sample count
			
			// where input > thresh and input is near max current input, add data. 		
			for(int j=0; j<mHeight; ++j)
			{
				for(int i=0; i<mWidth; ++i)
				{
					// test threshold with smoothed data
					float zSmooth = normTemp(i, j);
					// but add actual samples from unsmoothed input
					float z = input(i, j);
					if(zSmooth > peakZ * kNormalizeThreshold)
					{
						// map must = count * z/peakZ
						mTemp(i, j) = z / peakZ;
						mTemp2(i, j) = 1.0f;	
					}
				}
			}
			
			// add temp signals to data						
			mNormalizeMap.add(mTemp);
			mNormalizeCount.add(mTemp2);
			mVisSignal.copy(mNormalizeCount);
			mVisSignal.scale(1.f / (float)kNormMapSamples);
		}
		
		if(doneCollectingNormalizeMap())
		{				
			float mapMaximum = makeNormalizeMap();	
						
			MLConsole() << "\n****************************************************************\n\n";
			MLConsole() << "\n\nOK, done collecting normalize map. (max = " << mapMaximum << ").\n";
			MLConsole() << "Please lift your hands.";
			mCollectingNormalizeMap = false;
			mWaitSamplesAfterNormalize = 0;
			mVisSignal.clear();
			mStartupSum = 0;
			
			// MLTEST bail after normalize
			mHasCalibration = true;
			mActive = false;	
			r = 1;	
			
			MLConsole() << "\n****************************************************************\n\n";
			MLConsole() << "TEST: Normalization is now complete and will be auto-saved in the file \n";
			MLConsole() << "SoundplaneAppState.txt. \n";
			MLConsole() << "\n****************************************************************\n\n";
			
			
		}
	}
	else
	{
		if(mWaitSamplesAfterNormalize < waitAfterNormalize)
		{
			mStartupSum += peakZ;
			mWaitSamplesAfterNormalize++;
			if(mTotalSamples % 100 == 0)
			{
				MLConsole() << ".";
			}
		}
		else if(mWaitSamplesAfterNormalize == waitAfterNormalize)
		{
			mWaitSamplesAfterNormalize++;
			mAutoThresh *= 1.5f;
			MLConsole() << "\nOK, done collecting silence again (auto threshold: " << mAutoThresh << "). \n";

			MLConsole() << "\n****************************************************************\n\n";
			MLConsole() << "Now please slide a single finger over the  \n";
			MLConsole() << "Soundplane surface, visiting each area twice \n";
			MLConsole() << "until all the areas are colored green at left.  \n";
			MLConsole() << "Sliding over a key the first time will turn it gray.  \n";
			MLConsole() << "Sliding over a key the second time will turn it green.\n";
			MLConsole() << "\n";
		}
		else if(peakZ > mAutoThresh)
		{
			// normalize input
			mTemp.copy(input);
			mTemp.multiply(mNormalizeMap);
		
			// smooth input	
			mTemp.convolve3x3r(kc, ke, kk);
			mTemp.convolve3x3r(kc, ke, kk);
			mTemp.convolve3x3r(kc, ke, kk);
			
			// get corrected peak
			mPeak = mTemp.findPeak();
			mPeak = mTemp.correctPeak(mPeak.x(), mPeak.y(), 1.0f);							
			Vec2 minPos(2.0, 0.);
			Vec2 maxPos(mWidth - 2., mHeight - 1.);
			mPeak = vclamp(mPeak, minPos, maxPos);
		
			age++; // continue touch
			// get sample from input around peak and normalize
			mIncomingSample.clear();
			mIncomingSample.add2D(m, -mPeak + Vec2(kTemplateRadius, kTemplateRadius)); 
			mIncomingSample.sigMax(0.f);
			mIncomingSample.scale(1.f / mIncomingSample(kTemplateRadius, kTemplateRadius));

			// get integer bin	
			Vec2 binPeak = getBinPosition(mPeak);
			mVisPeak = binPeak - Vec2(0.5, 0.5);		
			int bix = binPeak.x();			
			int biy = binPeak.y();
			// clamp to calibratable area
			bix = clamp(bix, 2, mWidth - 2);
			biy = clamp(biy, 0, mHeight - 1);
			Vec2 bIntPeak(bix, biy);

			// count sum and minimum of all kernel samples for the bin
			int dataIdx = biy*mWidth + bix;
			mDataSum[dataIdx].add(mIncomingSample); 
			mData[dataIdx].sigMin(mIncomingSample); 
			mSampleCount[dataIdx]++;
            
			if(bIntPeak != intPeak1)
			{
				// entering new bin.
				intPeak1 = bIntPeak;
				if(mPassesCount[dataIdx] < kPassesToCalibrate)
				{
					mPassesCount[dataIdx]++;
					mVisSignal(bix, biy) = (float)mPassesCount[dataIdx] / (float)kPassesToCalibrate;
				}
			}			
			
			// check for done
			if(isDone())
			{
				mCalibrateSignal.setDims(kTemplateSize, kTemplateSize, mWidth*mHeight);
				
				// get result for each junction
				for(int j=0; j<mHeight; ++j)
				{
					for(int i=0; i<mWidth; ++i)
					{						
						int idx = j*mWidth + i;
						// copy to 3d signal
						mCalibrateSignal.setFrame(idx, mData[idx]);
					}
				}
				
				getAverageTemplateDistance();
				mHasCalibration = true;
				mActive = false;	
				r = 1;	
							
				MLConsole() << "\n****************************************************************\n\n";
				MLConsole() << "Calibration is now complete and will be auto-saved in the file \n";
				MLConsole() << "SoundplaneAppState.txt. \n";
				MLConsole() << "\n****************************************************************\n\n";
			}	
		}
		else
		{
			age = 0;
			intPeak1 = Vec2(-1, -1);
			mVisPeak = Vec2(-1, -1);
		}
	}
	mTotalSamples++;
	return r;
}

bool TouchTracker::Calibrator::isCalibrating()
{
	return mActive;
}

bool TouchTracker::Calibrator::hasCalibration()
{
	return mHasCalibration;
}

bool TouchTracker::Calibrator::isDone()
{
	// If we have enough samples at each location, we are done.
	bool calDone = true;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{
			if(isWithinCalibrateArea(i, j))
			{
				int dataIdx = j*mWidth + i;
				if (mPassesCount[dataIdx] < kPassesToCalibrate)
				{
					calDone = false;
					goto done;
				}
			}
		}
	}
done:	
	return calDone;
}

bool TouchTracker::Calibrator::doneCollectingNormalizeMap()
{
	// If we have enough samples at each location, we are done.
	bool calDone = true;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{
			if(isWithinCalibrateArea(i, j))
			{
				if (mNormalizeCount(i, j) < kNormMapSamples)
				{
					calDone = false;
					goto done;
				}
			}
		}
	}
done:	
	return calDone;
}
	
void TouchTracker::Calibrator::setCalibration(const MLSignal& v)
{
	if((v.getHeight() == kTemplateSize) && (v.getWidth() == kTemplateSize))
	{
        mCalibrateSignal = v;
        mHasCalibration = true;
    }
    else
    {
		MLConsole() << "TouchTracker::Calibrator::setCalibration: bad size, restoring default.\n";
        mHasCalibration = false;
    }
}

void TouchTracker::Calibrator::setNormalizeMap(const MLSignal& v)
{
	if((v.getHeight() == mSrcHeight) && (v.getWidth() == mSrcWidth))
	{
		mNormalizeMap = v;
		mHasNormalizeMap = true;
	}
	else
	{
		MLConsole() << "TouchTracker::Calibrator::setNormalizeMap: restoring default.\n";
		mNormalizeMap.fill(1.f);
		mHasNormalizeMap = false;
	}
}


float TouchTracker::Calibrator::getZAdjust(const Vec2 p)
{
	// first adjust z for interpolation based on xy position within unit square
	//
	Vec2 vInt, vFrac;
	p.getIntAndFracParts(vInt, vFrac);
	
	Vec2 vd = (vFrac - Vec2(0.5f, 0.5f));
	return 1.414f - vd.magnitude()*0.5f;
}

float TouchTracker::Calibrator::differenceFromTemplateTouch(const MLSignal& in, Vec2 pos)
{
	static MLSignal a2(kTemplateSize, kTemplateSize);
	static MLSignal b(kTemplateSize, kTemplateSize);
	static MLSignal b2(kTemplateSize, kTemplateSize);

	float r = 1.0;
	int height = in.getHeight();
	int width = in.getWidth();
	MLRect boundsRect(0, 0, width, height);
	
	// use linear interpolated z value from input
	float linearZ = in.getInterpolatedLinear(pos)*getZAdjust(pos);
	linearZ = clamp(linearZ, 0.00001f, 1.f);
	float z1 = 1./linearZ;	
	const MLSignal& a = getTemplate(pos);
	
	// get normalized input values surrounding touch
	int tr = kTemplateRadius;
	b.clear();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			Vec2 vInPos = pos + Vec2((float)i - tr,(float)j - tr);			
			if (boundsRect.contains(vInPos))
			{
				float inVal = in.getInterpolatedLinear(vInPos);
				inVal *= z1;
				b(i, j) = inVal;
			}
		}
	}
	
	int tests = 0;
	float sum = 0.;

	// add differences in z from template
	a2.copy(a);
	b2.copy(b);
    
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			if(b(i, j) > 0.)
			{
				float d = a2(i, j) - b2(i, j);
				sum += d*d;
				tests++;
			}
		}
	}

	// get RMS difference
	if(tests > 0)
	{
		r = sqrtf(sum / tests);
	}	
	return r;
} 

float TouchTracker::Calibrator::differenceFromTemplateTouchWithMask(const MLSignal& in, Vec2 pos, const MLSignal& mask)
{
	static float maskThresh = 0.001f;
	static MLSignal a2(kTemplateSize, kTemplateSize);
	static MLSignal b(kTemplateSize, kTemplateSize);
	static MLSignal b2(kTemplateSize, kTemplateSize);

	float r = 0.f;
	int height = in.getHeight();
	int width = in.getWidth();
	MLRect boundsRect(0, 0, width, height);
	
	// use linear interpolated z value from input
	float linearZ = in.getInterpolatedLinear(pos)*getZAdjust(pos);
	linearZ = clamp(linearZ, 0.00001f, 1.f);
	float z1 = 1./linearZ;	
	const MLSignal& a = getTemplate(pos);
	
	// get normalized input values surrounding touch
	int tr = kTemplateRadius;
	b.clear();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			Vec2 vInPos = pos + Vec2((float)i - tr,(float)j - tr);			
			if (boundsRect.contains(vInPos) && (mask.getInterpolatedLinear(vInPos) < maskThresh))
			{
				float inVal = in.getInterpolatedLinear(vInPos);
				inVal *= z1;
				b(i, j) = inVal;
			}
		}
	}
	
	int tests = 0;
	float sum = 0.;

	// add differences in z from template
	a2.copy(a);
	b2.copy(b);
	a2.partialDiffX();
	b2.partialDiffX();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			if(b(i, j) > 0.)
			{
				float d = a2(i, j) - b2(i, j);
				sum += d*d;
				tests++;
			}
		}
	}

	// get RMS difference
	if(tests > 0)
	{
		r = sqrtf(sum / tests);
	}	
	return r;
}



// --------------------------------------------------------------------------------
#pragma mark utilities

/*
void TouchTracker::dumpTouches()
{
	int c = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		const Touch& t = mTouches[i];
		if(t.isActive())
		{
			debug() << "t" << i << ":" << t << ", key#" << t.key << ", td " << t.tDist << ", dz " << t.dz << "\n";		
			c++;
		}
	}
	if(c > 0)
	{
		debug() << "\n";
	}
}
*/

/*
int TouchTracker::countActiveTouches()
{
	int c = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		const Touch& t = mTouches[i];
		if( t.isActive())
		{
			c++;
		}
	}
	return c;
}
*/

