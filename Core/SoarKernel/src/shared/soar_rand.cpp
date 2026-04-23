#include "soar_rand.h"

static MTRand gSoarRand;

// real number in [0,1]
double SoarRand()
{
    return gSoarRand.rand();
}

// real number in [0,n]
double SoarRand(const double& max)
{
    return gSoarRand.rand(max);
}

// integer in [0,2^32-1]
uint32_t SoarRandInt()
{
    return gSoarRand.randInt();
}

// integer in [0,n] for n < 2^32
uint32_t SoarRandInt(const uint32_t& max)
{
    return gSoarRand.randInt(max);
}


// automatically seed with a value based on the time or /dev/urandom
void SoarSeedRNG()
{
    gSoarRand.seed();
}

// seed with a provided value
void SoarSeedRNG(const uint32_t seed)
{
    gSoarRand.seed(seed);
}

uint32_t SoarRngStateWordCount()
{
    return static_cast<uint32_t>(MTRand::SAVE);
}

void SoarSaveRNGState(uint32_t* out_state_words, uint32_t word_count)
{
    if (!out_state_words || (word_count != static_cast<uint32_t>(MTRand::SAVE)))
    {
        return;
    }

    gSoarRand.save(out_state_words);
}

bool SoarLoadRNGState(const uint32_t* state_words, uint32_t word_count)
{
    if (!state_words || (word_count != static_cast<uint32_t>(MTRand::SAVE)))
    {
        return false;
    }

    uint32_t load_state[MTRand::SAVE] = {0};
    for (uint32_t i = 0; i < word_count; ++i)
    {
        load_state[i] = state_words[i];
    }

    gSoarRand.load(load_state);
    return true;
}
