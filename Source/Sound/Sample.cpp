#include "StdAfxSound.h"
#include "SoundInternal.h"
#include "Sample.h"
#include "../Render/inc/RenderMT.h"
#include <unordered_map>

///Used for tracking what channel is playing what sample, if sample is not here then is not being played
std::vector<SND_Sample*> channelSamples;

/*
 * TODO replace this lock usage with proper SDL_LockAudioDevice/SDL_UnlockAudioDevice
 * once there is a way to obtain SDL_AudioDeviceID from SDL_Mixer (port game to SDL3?)
 */
MTSection channelSamplesLock;

// make a channelDone function
static void callbackChannelFinished(int channel)
{
    //printf("Channel %d finished playing.\n", channel);
    MTAuto mtenter(&channelSamplesLock);
    channelSamples[channel] = nullptr;
}

void SNDSetupChannelCallback(int mixChannels, bool init) {
    Mix_ChannelFinished(init ? callbackChannelFinished : nullptr);
    if (init) {
        channelSamples.resize(mixChannels, nullptr);
    } else {
        channelSamples.clear();
    }
}

MixChunkWrapper::~MixChunkWrapper() {
    if (chunk) {
        //Only free with Mix if sound is inited, check Mix_Init just in case
        if (SND::has_sound_init || (Mix_Init(0) != 0)) {
            Mix_FreeChunk(chunk);
        } else {
            SDL_free(chunk);
        }
        chunk = nullptr;
    }
}

SND_Sample::SND_Sample(const std::shared_ptr<MixChunkWrapper>& chunk)  {
    this->chunk = chunk;
    this->chunk_source = chunk;
    Mix_Chunk* current = getChunk();
    if (current) {
        this->chunk_millis = SNDcomputeAudioLengthMS(current->alen);
    } else {
        this->chunk_millis = 0;
    }
}

SND_Sample::~SND_Sample() {
    chunk_source = nullptr;
    chunk = nullptr;
}

bool SND_Sample::loadRawData(uint8_t* src_data, size_t src_len, bool copy, const std::string& file_name) {
    Mix_Chunk* new_chunk = (Mix_Chunk*) SDL_malloc(sizeof(Mix_Chunk));
    new_chunk->allocated = 1;
    if (copy) {
        new_chunk->abuf = (Uint8*) SDL_calloc(1, src_len);
        SDL_memcpy(new_chunk->abuf, src_data, src_len);
    } else {
        new_chunk->abuf = src_data;
    }
    new_chunk->alen = src_len;
    if (chunk_source) {
        new_chunk->volume = chunk_source->chunk->volume;
    } else {
        new_chunk->volume = 128;
    }
    chunk_source = chunk = std::make_shared<MixChunkWrapper>(new_chunk, file_name);
    this->chunk_millis = SNDcomputeAudioLengthMS(src_len);
    this->chunk_frequency = this->frequency;
    return true;
}

int SND_Sample::play() {
    int channel = getChannel();
    //Don't play if already playing
    if (channel != SND_NO_CHANNEL) {
        return SND_NO_CHANNEL;
    }
    
    //Print using channels
    //fprintf(stderr, "Playing: %d\n", Mix_Playing(-1));
    
    //Find a available channel
    if (channel_group == SND_NO_CHANNEL) {
        fprintf(stderr, "SND_Sample channel_group not set!\n");
        channel_group = SND_GROUP_EFFECTS;
    }
    channel = Mix_GroupAvailable(channel_group);
    if (channel == SND_NO_CHANNEL && channel_group == SND_GROUP_EFFECTS) {
        //Avoid playing if volume is too low since we are starved already
        if (this->volume < SND::EFFECT_VOLUME_THRESHOLD) {
            return SND_NO_CHANNEL;
        }
        
        //Use the specific groups
        channel = Mix_GroupAvailable(this->looped ? SND_GROUP_EFFECTS_LOOPED : SND_GROUP_EFFECTS_ONCE);
    }
    
    //Steal if necessary
    if (channel == SND_NO_CHANNEL && steal_channel) {
        channel = Mix_GroupOldest(this->channel_group);
        if (channel != SND_NO_CHANNEL) {
            Mix_HaltChannel(channel);
        }
    }
    
    //Play if found
    if (channel != SND_NO_CHANNEL) {
        //Setup frequency
        this->frequency = std::max(0.0f, std::min(50.0f, this->frequency));
        if (needFrequencyChange()) {
            this->convertChunkFrequency();
        }
        
        //Setup channel that is going to play this sound
        this->updateEffects(channel);

        //Play audio sample
        Mix_Chunk* chunk_play = getChunk();
        chunk_play->volume = getChunkSource()->volume;
        bool loop = this->external_looped_restart ? false : this->looped; //Set loop flag if not externally controlled 
        channel = Mix_PlayChannel(channel, chunk_play, loop ? -1 : 0);
        if (channel == -1) { //Return's -1 if fails to play
            fprintf(stderr, "Mix_PlayChannel error (%s): %s\n",  chunk->fileName.c_str(), Mix_GetError());
            channel = SND_NO_CHANNEL;
        } else {
            //Store channel for callback
            //SDL_LockAudioDevice();
            xassert(channel < channelSamples.size());
            MTAuto mtenter(&channelSamplesLock);
            xassert(channelSamples[channel] == nullptr);
            channelSamples[channel] = this;
            //SDL_UnlockAudioDevice();
        }
        
    }
#if 0
    if (channel == SND_NO_CHANNEL) {
        fprintf(stderr, "SND_Sample channel not found\n");
    }
#endif

    return channel;
}

bool SND_Sample::updateEffects(int channel) {
    bool updated = false;
    if (channel != SND_NO_CHANNEL) {
        //Setup volume
        this->volume = std::max(0.0f, std::min(1.0f, this->volume));
        float global_vol = 1.0f;
        switch (global_volume_select) {
            default:
            case GLOBAL_VOLUME_IGNORE:
                break;
            case GLOBAL_VOLUME_CHANNEL:
                global_vol = channel_group == SND_GROUP_SPEECH ? 1.0f : SND::sound_volume;
                break;
            case GLOBAL_VOLUME_VOICE:
                global_vol = SND::voice_volume;
                break;
            case GLOBAL_VOLUME_EFFECTS:
                global_vol = SND::sound_volume;
                break;
        }
        Mix_Volume(channel, static_cast<int>(128.0f * this->volume * global_vol));

        //Setup panning
        this->pan = std::max(0.0f, std::min(1.0f, this->pan));
        int right = static_cast<int>(255 * this->pan);
        Mix_SetPanning(channel, 255 - right, right);
        
        updated = true;
    }
    return updated;
}

bool SND_Sample::updateEffects() {
    int channel = getChannel();

    //Check if frequency changed 
    if (channel != SND_NO_CHANNEL && needFrequencyChange()) {
        //Okay this sound is not gonna stop so we need to stop first to update frequency at play
        if (this->looped && !this->external_looped_restart) {
            if (stop()) {
                //We need to restart manually
                //fprintf(stderr, "%p Frequency changed %f -> %f\n", this, this->chunk_frequency, this->frequency);
                return play() != SND_NO_CHANNEL;
            }
        }
    }

    return updateEffects(channel);
}

int SND_Sample::getChannel() const {
    if (!SND::has_sound_init) return SND_NO_CHANNEL;
    //SDL_LockAudioDevice();
    MTAuto mtenter(&channelSamplesLock);
    int channel = SND_NO_CHANNEL;
    for (int i = 0; i < channelSamples.size(); ++i) {
        if (channelSamples[i] == this) {
            channel = i;
            break;
        }
    }
    //SDL_UnlockAudioDevice();
    return channel;
}

bool SND_Sample::isPlaying() const {
    int channel = getChannel();
    return channel != SND_NO_CHANNEL;
}

bool SND_Sample::isPaused() const {
    int channel = getChannel();
    return channel != SND_NO_CHANNEL && Mix_Paused(channel);
}

bool SND_Sample::pause() const {
    int channel = getChannel();
    if (channel != SND_NO_CHANNEL) {
        Mix_Pause(channel);
        return true;
    }
    return false;
}

bool SND_Sample::resume() const {
    int channel = getChannel();
    if (channel != SND_NO_CHANNEL) {
        Mix_Resume(channel);
        return true;
    }
    return false;
}

bool SND_Sample::stop() const {
    int channel = getChannel();
    if (channel != SND_NO_CHANNEL) {
        Mix_HaltChannel(channel);
        return true;
    }
    return false;
}

bool SND_Sample::needFrequencyChange() const {
    return std::abs(this->frequency - this->chunk_frequency) > 0.10;
}

bool SND_Sample::convertChunkFrequency() {
    int new_frequency = static_cast<int>(static_cast<float>(SND::deviceFrequency) * this->frequency);

    //Build the audio converter to convert device format (chunks are loaded with this format already) into desired format
    SDL_AudioCVT cvt;
    int res = SDL_BuildAudioCVT(
            &cvt,
            //Source chunk format
            SND::deviceFormat, SND::deviceChannels, SND::deviceFrequency,
            //Destination format, we basically want new frequency
            SND::deviceFormat, SND::deviceChannels, new_frequency
    );
    if (res < 0) {
        SDL_PRINT_ERROR("SND_Sample SDL_BuildAudioCVT");
    } else if (cvt.needed) {
        Mix_Chunk* source = getChunkSource();

        //We need to allocate buffer for conversion
        cvt.len = static_cast<int>(source->alen);
        cvt.buf = (Uint8*) SDL_calloc(1, cvt.len * cvt.len_mult);
        if (cvt.buf == nullptr) {
            ErrH.Abort("Out of memory");
        }

        //Copy audio data to buffer
        SDL_memcpy(cvt.buf, source->abuf, cvt.len);

        //Convert the audio and replace old chunk audio with converted data
        if (SDL_ConvertAudio(&cvt) < 0) {
            SDL_PRINT_ERROR("SND_Sample SDL_BuildAudioCVT");
        } else {
            Mix_Chunk* new_chunk = (Mix_Chunk *) SDL_malloc(sizeof(Mix_Chunk));
            new_chunk->allocated = 1;
            new_chunk->volume = source->volume;
            new_chunk->abuf = cvt.buf;
            new_chunk->alen = cvt.len_cvt;
            chunk = std::make_shared<MixChunkWrapper>(new_chunk, chunk_source ? chunk_source->fileName : "");
            this->chunk_millis = SNDcomputeAudioLengthMS(cvt.len_cvt);
            this->chunk_frequency = this->frequency;
            return true;
        }
    }
    return false;
}
