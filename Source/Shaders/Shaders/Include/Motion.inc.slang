#ifndef NOVUS_MOTION_INCLUDED
#define NOVUS_MOTION_INCLUDED

// Half-float NaNs distinguish missing history from genuine zero motion.
static const uint INVALID_MOTION = 0x7E007E00u;

uint PackMotion(float2 motion)
{
    return all(isfinite(motion)) ? f32tof16(motion.x) | (f32tof16(motion.y) << 16u) : INVALID_MOTION;
}

bool UnpackValidMotion(uint packed, out float2 motion)
{
    motion = float2(f16tof32(packed & 0xFFFFu), f16tof32(packed >> 16u));
    return all(isfinite(motion));
}
#endif
