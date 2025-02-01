#include "StdAfxSound.h"
#include "PerimeterSound.h"
#include "SoundInternal.h"
#include "../Render/inc/RenderMT.h"
#include "C3D.h"
#include "SoundScript.h"
#include "Sample.h"
#include "files/files.h"

//Audio formats
#define AUDIO_FORMAT_8 AUDIO_S8
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define AUDIO_FORMAT_16 AUDIO_S16LSB
#else
#define AUDIO_FORMAT_16 AUDIO_S16MSB
#endif

static bool g_enable_sound = false;
static bool g_enable_voices = true;
static bool enable_sound_log = false;

SND3DListener snd_listener;

static std::string sound_directory="";

namespace SND {
float sound_volume = 1.0f;
float voice_volume = 1.0f;
int deviceFrequency = 0;
int deviceChannels = 0;
Uint16 deviceFormat = 0;
bool has_sound_init = false;

static float width2d=1,power2d_width=1;

float PanByX(float x)
{
    if(x<0)x=0;
    if(x>width2d)x=width2d;
    float xx= (2.0f * x - width2d) * power2d_width;
    xx = power2d_width + xx / 2.0f;

    return xx;
}

void logs(const char *format, ...)
{
    if (!enable_sound_log) return;
    va_list args;
    char    buffer[512];
    va_start(args,format);
    vsnprintf(buffer,512,format,args);
    fprintf(stderr, "%s", buffer);
}

int pause_level=0;
};

using namespace SND;

void SND2DPanByX(float width,float power) {
    width2d = width;
    power2d_width = power/width;
}

void SNDSetLocDataDirectory(const char* dir)
{
	SNDScript::setLocDataPath(dir);
}

void SNDSetBelligerentIndex(int idx)
{
	SNDScript::setBelligerentIndex(idx);
}

void SNDEnableSound(bool enable)
{
	g_enable_sound = enable && has_sound_init;
	if(!enable && has_sound_init) {
        SNDStopAll();
    }
}

void SNDEnableVoices(bool enable)
{
	g_enable_voices = enable && has_sound_init;
	if(!enable && has_sound_init) {
        script2d.StopAllVoices();
    }
}

bool SNDIsVoicesEnabled() {
	return g_enable_voices;
}

void SNDSetSoundDirectory(const char* dir)
{
	sound_directory = dir;
}

const std::string& SNDGetSoundDirectory()
{
	return sound_directory;
}

void AllocateMixChannel(int channel, int group) {
    //printf("AllocateMixChannel %d -> %d\n", channel, group);
    int ret = Mix_GroupChannel(channel, group);
    if (ret != 1) {
        fprintf(stderr, "Error AllocateMixChannel %d -> %d ret = %d\n", channel, group, ret);
    }
}

bool SNDInitSound(int mixChannels, int chunkSizeFactor)
{
	SNDReleaseSound();

    int flags = MIX_INIT_OGG;
    int initted = Mix_Init(flags);
    if ((initted&flags) != flags) {
        fprintf(stderr, "Mix_Init: Failed to init required ogg support %s\n", Mix_GetError());
    }

    bool open_audio_ok = false;
#ifdef GPX
    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 1, 1024) == 0) {
        open_audio_ok = true;
    } else {
        fprintf(stderr, "Mix_OpenAudio error open audio: %s\n", Mix_GetError());
    }
#else
    //Choose audio device
	struct FORMATS
	{
		int channels;
		int hertz;
		int bits;
	}
    formats[] = {
		{1,22050,AUDIO_FORMAT_8},
		{1,22050,AUDIO_FORMAT_16},
        {1,44100,AUDIO_FORMAT_16},
        {2,22050,AUDIO_FORMAT_8},
		{2,22050,AUDIO_FORMAT_16},
		{2,44100,AUDIO_FORMAT_16},
	};

	for(int i=SIZE(formats)-1;i>=0;i--) {
        int chunksize = chunkSizeFactor * (formats[i].hertz / 1000) * formats[i].channels;
        chunksize *= AUDIO_FORMAT_8 == formats[i].bits ? 1 : 2;
        if (Mix_OpenAudio(formats[i].hertz, formats[i].bits, formats[i].channels, chunksize) == 0) {
            open_audio_ok = true;
            break;
        } else {
            fprintf(stderr, "Mix_OpenAudio error with format %i: %s\n", i, Mix_GetError());
        }
	}
#endif
    
    if (!open_audio_ok) {
        logs("All Mix_OpenAudio failed!\n");
        return false;
    }
    
    //Query format info of device
    int numtimesopened = Mix_QuerySpec(&deviceFrequency, &deviceFormat, &deviceChannels);
    if(!numtimesopened) {
        fprintf(stderr, "Mix_QuerySpec error: %s\n",Mix_GetError());
        return false;
    } else {
        const char* format_str;
        switch(deviceFormat) {
            default: format_str="Unknown"; break;
            case AUDIO_U8: format_str="U8"; break;
            case AUDIO_S8: format_str="S8"; break;
            case AUDIO_U16LSB: format_str="U16LSB"; break;
            case AUDIO_S16LSB: format_str="S16LSB"; break;
            case AUDIO_U16MSB: format_str="U16MSB"; break;
            case AUDIO_S16MSB: format_str="S16MSB"; break;
        }
        printf("Audio opened=%d times frequency=%dHz format=%s channels=%d\n", numtimesopened, deviceFrequency, format_str, deviceChannels);
    }

    has_sound_init = true;

    initclock();

    //Allocate and reserve all channels for groups
    Mix_AllocateChannels(mixChannels);
    Mix_ReserveChannels(mixChannels);
    
    //Reserve one for speech
    int index = 0;
    AllocateMixChannel(index++, SND_GROUP_SPEECH);
    if (4 <= (mixChannels - index)) {
        //Reserve effects per type
        int looped = index + static_cast<int>(mixChannels * 0.30);
        for (; index < looped; index++) {
            AllocateMixChannel(index, SND_GROUP_EFFECTS_LOOPED);
        }
        int once = index + static_cast<int>(mixChannels * 0.30);
        for (; index < once; index++) {
            AllocateMixChannel(index, SND_GROUP_EFFECTS_ONCE);
        }
    }
    //Assign the rest to common effects group
    for (; index < mixChannels; index++) {
        AllocateMixChannel(index, SND_GROUP_EFFECTS);
    }
    
    SNDSetupChannelCallback(mixChannels, true);

	pause_level = 0;

	return true;
}

void SNDReleaseSound() 
{
	if (!has_sound_init) return;

    SNDSetupChannelCallback(0, false);

	script3d.RemoveAll();
	script2d.RemoveAll();

    Mix_CloseAudio();

    Mix_Quit();

    //Do at the end so Mix_FreeChunk is called
    has_sound_init = false;
}

void SNDEnableErrorLog(bool enable)
{
    enable_sound_log = enable; 
}

void SNDSetSoundVolume(float volume)
{
    sound_volume = std::max(0.0f, std::min(1.0f, volume));
    SNDEnableSound(0 < sound_volume);
    SNDUpdateAllSoundVolume();
}

void SNDSetVoiceVolume(float volume)
{
    voice_volume = std::max(0.0f, std::min(1.0f, volume));
    SNDEnableVoices(0 < voice_volume);
    SNDUpdateAllSoundVolume();
}

bool SNDScriptPrmEnable(const SoundScriptPrm& prm)
{
	if(prm.is3D)
		return script3d.AddScript(&prm);
	else
		return script2d.AddScript(&prm);
}

bool SNDScriptPrmEnableAll()
{
    SingletonPrm<SoundScriptTable>::load();
	for (const auto& i : soundScriptTable().table) {
        SNDScriptPrmEnable(i);
    }

	return true;
}

SND_Sample* SNDLoadSound(const std::string& fxname)
{
	if(!SND::has_sound_init) {
        return nullptr;
    }

    std::string path = convert_path_content(fxname);
    if (path.empty()) {
        return nullptr;
    }
    
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if(!chunk) {
        fprintf(stderr, "Mix_LoadWAV error %s : %s\n", fxname.c_str(), Mix_GetError());
        return nullptr;
    }

    auto wrapper = std::make_shared<MixChunkWrapper>(chunk, fxname);
    auto* sample = new SND_Sample(wrapper);
    return sample;
}

void SNDUpdateAllSoundVolume()
{
	SNDScript::MapScript::iterator it;
	FOR_EACH(script3d.map_script,it)
	{
		ScriptParam* sp=(*it).second;
		MTAuto lock(sp->GetLock());
		std::vector<SNDOneBuffer>::iterator itb;
		FOR_EACH(sp->GetBuffer(),itb)
		{
			(*itb).RecalculateVolume();
		}
	}

	FOR_EACH(script2d.map_script,it)
	{
		ScriptParam* sp=(*it).second;
		MTAuto lock(sp->GetLock());
		std::vector<SNDOneBuffer>::iterator itb;
		FOR_EACH(sp->GetBuffer(),itb)
		{
			(*itb).RecalculateVolume();
		}
	}
}

///////////////////////SND3DListener////////////////////////
SND3DListener::SND3DListener()
:velocity(0,0,0)
{
	rotate=invrotate=Mat3f::ID;
	position.x=position.y=position.z=0;
	//mat=MatXf::ID;
	s_distance_factor=1.0;//DS3D_DEFAULTDISTANCEFACTOR
	s_doppler_factor=1.0;//DS3D_DEFAULTDOPPLERFACTOR
	s_rolloff_factor=1.0;//DS3D_DEFAULTROLLOFFFACTOR

	velocity.x=velocity.y=velocity.z=0;

	front.x=0;
	front.y=0;
	front.z=1.0;
	top.x=0;
	top.y=1.0;
	top.z=0;
	right.x=-1.0;
	right.y=0;
	right.z=0;

	zmultiple=1.0f;
}

SND3DListener::~SND3DListener()
{
}

bool SND3DListener::SetDistanceFactor(float f)
{
	s_distance_factor=f;
	return true;
}

bool SND3DListener::SetDopplerFactor(float f)
{
	s_doppler_factor=f;
	return true;
}

bool SND3DListener::SetRolloffFactor(float f)
{
	s_rolloff_factor=f;
	return true;
}

bool SND3DListener::SetPos(const MatXf& _mat)
{
	Vect3f t=_mat.trans();

	rotate=_mat.rot();
	invrotate.invert(rotate);
	position=invrotate*t;
	position=-position;

//	front=invrotate*Vect3f(0,0,1.0f);;
//	top=invrotate*Vect3f(0,1.0f,0);
//	right=invrotate*Vect3f(-1.0,0,0);

	return true;
}

bool SND3DListener::SetVelocity(const Vect3f& _velocity)
{
	velocity=_velocity;
	return true;
}

bool SNDOneBuffer::RecalculatePos()
{
	if (!used) return true;
	//Vect3f p=snd_listener.mat*pos;
	Vect3f p=snd_listener.rotate*(pos-snd_listener.position);
	bool ok = p3DBuffer->SetPosition(pos);//p);

//	Vect3f v=snd_listener.mat.rot()*velocity;
	Vect3f v=snd_listener.rotate*velocity;
	ok &= p3DBuffer->SetVelocity(v);
	p3DBuffer->RecalculatePos();

	return ok;

}

void SNDOneBuffer::RecalculateVolume()
{
	if(!used)return;
	
	if(p3DBuffer)
	{
		p3DBuffer->SetVolume(volume);
		p3DBuffer->RecalculateVolume();
	}else
	{
		buffer->volume = volume;
        buffer->updateEffects();
	}
}

void SNDOneBuffer::PlayPreprocessing()
{
	if(script->delta_enable)
	{
		if(script->delta_random)
		{
			float dw=script->delta_up-script->delta_down;
            float rnd=static_cast<float>(rand())/static_cast<float>(RAND_MAX);
			float freqmul=rnd*dw+script->delta_down;

			if(p3DBuffer)
				p3DBuffer->SetFrequency(freqmul);
			else
				buffer->frequency = freqmul;
		}
	}
}


bool SND3DListener::Update()
{
	SNDScript::MapScript::iterator it;
	FOR_EACH(script3d.map_script,it)
	{
		ScriptParam* sp=(*it).second;
		MTAuto lock(sp->GetLock());
		sp->RecalculateClipDistance();

		std::vector<SNDOneBuffer>::iterator itb;
		FOR_EACH(sp->GetBuffer(),itb)
		{
			(*itb).RecalculatePos();
		}
	}

	return true;
}

///////////////////////SND3DSound////////////////////////
SND3DSound::SND3DSound()
{
	script=NULL;
	cur_buffer=-1;
}

void SND3DSound::Destroy()
{
	if(has_sound_init && script && cur_buffer>=0)
	{
		Stop();
		MTAuto lock(script->GetLock());
		SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
		ASSERT(s.pSound==this);
		s.used=false;
		s.pSound=NULL;
	}

	script=NULL;
	cur_buffer=-1;
}

SND3DSound::~SND3DSound()
{
	Destroy();
}

bool SND3DSound::Init(const char* name)
{
	Destroy();
	if (!SND::has_sound_init) {
        return false;
    }
	bool b=script3d.FindFree(name,script,cur_buffer);
	if(!b)return false;

	MTAuto lock(script->GetLock());
	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	s.pSound=this;

	s.pos.x=s.pos.y=s.pos.z=0;
	s.velocity.x=s.velocity.y=s.velocity.z=0;
	s.volume=script->def_volume;

	s.p3DBuffer->SetMinDistance(script->radius);
	s.p3DBuffer->SetMaxDistance(script->max_radius);

	return true;
}

void SND3DSound::SetRealVolume(float vol)
{
    if (script == nullptr) {
        return;
    }

	MTAuto lock(script->GetLock());
	AssertValid();
	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	if(vol>1)vol=1;
	if(vol<0)vol=0;
	s.volume=vol;
	s.RecalculateVolume();
}

void SND3DSound::SetVolume(float vol)
{
    if (script == nullptr) {
        return;
    }

	float v=vol*(script->def_volume-script->min_volume)+script->min_volume;
	SetRealVolume(v);
}

bool SND3DSound::SetFrequency(float frequency)
{
	if (script == nullptr) {
        return false;
    }
	MTAuto lock(script->GetLock());
	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	return s.SetFrequency(frequency);
}

void SND3DSound::AssertValid()
{
	ASSERT(script);
	ASSERT(cur_buffer>=0 && cur_buffer<script->GetBuffer().size());
}

void SND3DSound::SetPos(const Vect3f& pos)
{
    if (script == nullptr) {
        return;
    }
	MTAuto lock(script->GetLock());
	AssertValid();
	Vect3f& p=script->GetBuffer()[cur_buffer].pos;
	p=pos;
}

void SND3DSound::SetVelocity(const Vect3f& velocity)
{
    if (script == nullptr) {
        return;
    }
	MTAuto lock(script->GetLock());
	AssertValid();
	script->GetBuffer()[cur_buffer].velocity=velocity;
}

bool SND3DSound::Play(bool cycled)
{
    if (script == nullptr
    || (!g_enable_sound && !script->language_dependency)
    || (!g_enable_voices && script->language_dependency)) {
        return false;
    }
	MTAuto lock(script->GetLock());
	AssertValid();
	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];

	s.PlayPreprocessing();
	s.RecalculatePos();
	s.RecalculateVolume();

	s.played_cycled=cycled;

	s.begin_play_time=clockf();
	return s.p3DBuffer->Play(cycled);
}

bool SND3DSound::Stop()
{
	if (script == nullptr) {
        return true;
    }
	MTAuto lock(script->GetLock());
	AssertValid();
	return script->GetBuffer()[cur_buffer].p3DBuffer->Stop();
}

bool SND3DSound::IsPlayed()
{
	if (script == nullptr) {
        return false;
    }
	MTAuto lock(script->GetLock());
	return script->GetBuffer()[cur_buffer].p3DBuffer->IsPlaying();
}

////////////////////////////////////////////////////////////

bool SNDScript::FindFree(const char* name, ScriptParam*& script, int& nfree)
{
	script=NULL;
	nfree=-1;

	ScriptParam* p=Find(name);

	if(p==NULL)
	{
		logs("sound %s not found\n",name);
		return false;
	}
	MTAuto lock(p->GetLock());
	if(p->GetSounds().empty())
		return false;

	std::vector<SNDOneBuffer>& sb=p->GetBuffer();

	for(int i=0;i<sb.size();i++)
	{
		SNDOneBuffer* p=&sb[i];
		if(!p->used)
		{
			nfree=i;
			break;
		}

		if(p->pSound==NULL && p->pause_level==0)
		{
			if(p->p3DBuffer)
			{
				if(!p->p3DBuffer->IsPlaying())
				{
					nfree=i;
					break;
				}
			}else
			{
				if (!p->buffer->isPlaying()) {
                    nfree=i;
                    break;
				}
			}
		}
	}

	if(0){
		int played=0,real_played=0;
		for(int i=0;i<sb.size();i++)
		{
			SNDOneBuffer* p=&sb[i];
			if(p->pSound==NULL && p->pause_level==0)
			{
				if(p->p3DBuffer)
				{
					if(p->p3DBuffer->IsPlaying())
						played++;
				}

                if (p->buffer->isPlaying()) {
                    real_played++;
                }
			}
		}
		logs("name=%s size=%i, played=%i, real=%i\n",p->sound_name,sb.size(),played,real_played);
	}

	if(nfree<0)
	{
		if(b2d && sb.size()>=p->max_num_sound)
			return false;
		if(!b2d && p->max_num_sound==1 && sb.size()>0)
			return false;

		SNDOneBuffer s;
		s.script=p;

		nfree=sb.size();
		sb.push_back(s);
	}

	if(nfree<0)
		return false;

	SNDOneBuffer& s=sb[nfree];
	s.used=true;
	s.pSound=NULL;
	s.script=p;

	script=p;

	if(s.buffer==NULL || p->GetSounds().size()>1)
	{
		int n = 0;

		if(p->belligerent_dependency){
			n = p->belligerentIndex(belligerentIndex);
			int size = (belligerentIndex < soundScriptTable().belligerentPrefix.size() - 1) ? p->belligerentIndex(belligerentIndex + 1) - n : p->GetSounds().size() - n;

			if(size)
				n += (rand()%size);
		}
		else
			n = rand()%p->GetSounds().size();

		xassert(n >= 0 && n < p->GetSounds().size());
		if(n < 0) n = 0;
		if(n >= p->GetSounds().size()) n = p->GetSounds().size() - 1;

		SAFE_DELETE(s.buffer);

        SND_Sample* sample = p->GetSounds().at(n);
        if(!sample)
            return false;
		s.buffer = new SND_Sample(*sample);
		s.nSamplesPerSec=sample->getChunkSource()->alen;

		if(!b2d)
		{
			s.p3DBuffer=new SoftSound3D;

			if(!s.p3DBuffer->Init(s.buffer))
				return false;
		}

	}

	return true;
}

bool SND3DPlaySound(const char* name,
                    const Vect3f* pos,
                    const Vect3f* velocity//По умолчанию объект считается неподвижным
					)
{
	if (!SND::has_sound_init) {
        return true;
    }
	if (pos == nullptr) {
        return false;
    }
	
	ScriptParam* script;
	int nfree;

	bool b=script3d.FindFree(name,script,nfree);
	if(!b)
		return false;

	MTAuto lock(script->GetLock());

	SNDOneBuffer& s=script->GetBuffer()[nfree];
	s.pos=*pos;

	if (s.p3DBuffer->SetMinDistance(script->radius) && s.p3DBuffer->SetMaxDistance(script->max_radius)) {
        if(velocity) {
            s.velocity = *velocity;
        } else {
            s.velocity.x = s.velocity.y = s.velocity.z = 0;
        }

        s.volume=script->def_volume;

        s.PlayPreprocessing();

        if (s.RecalculatePos()) {
            s.RecalculateVolume();

            if (s.p3DBuffer->Play(false)) {
                s.begin_play_time = clockf();
                s.played_cycled = false;

                return true;
            }
        }
    }
    
	s.used=false;
	return false;
}

/////////////////////////2D//////////////////////////
bool SND2DPlaySound(const char* name,float x)
{
	if (!SND::has_sound_init || !name) {
        return false;
    }
    xassert(0 <= x && x <= 1.0f);
	ScriptParam* script;
	int nfree;

	bool b=script2d.FindFree(name,script,nfree);
	if(!b || (!g_enable_voices && script->language_dependency)) {
        return false;
    }

	MTAuto lock(script->GetLock());

	SNDOneBuffer& s=script->GetBuffer()[nfree];
    //printf("SND2DPlaySound %p pos %f name %s\n", &s, x, name);
	s.pos.x=x;
	s.volume=script->def_volume;

	s.RecalculateVolume();
    
	s.PlayPreprocessing();

	s.buffer->pan = PanByX(x);
    s.buffer->global_volume_select = script->language_dependency
                                     ? GLOBAL_VOLUME_VOICE
                                     : GLOBAL_VOLUME_CHANNEL;

	s.begin_play_time=clockf();
    s.played_cycled = false;
    int channel = s.buffer->play();
	if (channel == SND_NO_CHANNEL) {
        s.used=false;
        return false;
    }

	return true;
}

SND2DSound::SND2DSound()
{
	script=NULL;
	cur_buffer=-1;
}

SND2DSound::~SND2DSound()
{
	Destroy();
}

bool SND2DSound::Init(const char* name)
{
	Destroy();
	if (!SND::has_sound_init) {
        return false;
    }
	bool b=script2d.FindFree(name,script,cur_buffer);
	if (!b) return false;

	MTAuto lock(script->GetLock());

	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	s.pSound=this;

	s.pos.x=s.pos.y=s.pos.z=0;
	s.velocity.x=s.velocity.y=s.velocity.z=0;
	s.volume=script->def_volume;
	return true;
}

bool SND2DSound::Play(bool cycled)
{
    if (script == nullptr
	|| (!g_enable_sound && !script->language_dependency)
    || (!g_enable_voices && script->language_dependency)) {
        return false;
    }
    
	MTAuto lock(script->GetLock());
	AssertValid();
	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	s.PlayPreprocessing();

	s.RecalculateVolume();

    s.buffer->global_volume_select = script->language_dependency
                                     ? GLOBAL_VOLUME_VOICE
                                     : GLOBAL_VOLUME_CHANNEL;
    s.buffer->looped = cycled;
    int channel = s.buffer->play();
    if (channel == SND_NO_CHANNEL) {
        return false;
    }
	s.begin_play_time=clockf();
	s.played_cycled = cycled;
	return true;
}

bool SND2DSound::Stop()
{
	if (script== nullptr) {
        return true;
    }
	MTAuto lock(script->GetLock());
	AssertValid();
	return script->GetBuffer()[cur_buffer].buffer->stop();
}

bool SND2DSound::IsPlayed() const
{
	if(script==NULL)return false;
	MTAuto lock(script->GetLock());

	SND_Sample* buffer=script->GetBuffer()[cur_buffer].buffer;
	if(buffer==NULL)return false;
    return buffer->isPlaying();
}

bool SND2DSound::SetPos(float x)
{
	if (script == nullptr) {
        return false;
    }
	MTAuto lock(script->GetLock());

	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	s.buffer->pan = PanByX(x);
	return true;
}

bool SND2DSound::SetFrequency(float frequency)
{
	if (script == nullptr) {
        return false;
    }
	MTAuto lock(script->GetLock());

	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];

	return s.SetFrequency(frequency);
}

void SND2DSound::SetVolume(float vol)
{
    if (script == nullptr) {
        return;
    }
	float v=vol*(script->def_volume-script->min_volume)+script->min_volume;
	SetRealVolume(v);
}

void SND2DSound::SetRealVolume(float vol)
{
    if (script == nullptr) {
        return;
    }
	MTAuto lock(script->GetLock());

	AssertValid();
	SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
	if(vol>1)vol=1;
	if(vol<0)vol=0;
	s.volume=vol;
	s.RecalculateVolume();
}

void SND2DSound::AssertValid()
{
	ASSERT(script);
	ASSERT(cur_buffer>=0 && cur_buffer<script->GetBuffer().size());
}

void SND2DSound::Destroy()
{
	if(has_sound_init && script && cur_buffer>=0)
	{
		Stop();
		MTAuto lock(script->GetLock());

		SNDOneBuffer& s=script->GetBuffer()[cur_buffer];
		ASSERT(s.pSound==this);
		s.used=false;
		s.pSound=NULL;
	}
}


void SNDStopAll()
{
	script3d.StopAllPlayed();
	script2d.StopAllPlayed();
}

int SNDDeviceFrequency() {
    return SND::deviceFrequency;
}

int SNDDeviceChannels() {
    return SND::deviceChannels;
}

SDL_AudioFormat SNDDeviceFormat() {
    return SND::deviceFormat;
}

uint64_t SNDcomputeAudioLengthUS(uint64_t bytes) {
    //Sometimes audio might not be initialized when calling this, avoid zerodiv
    if (!has_sound_init) {
        return 0;
    }
    
    //Code based from Carlos Faruolo https://gist.github.com/hydren/f60d107f144fcb41dd6f898b126e17b2
    /* bytes / samplesize == sample points */
    const uint64_t points = bytes / SNDformatSampleSize(SND::deviceFormat);

    /* sample points / channels == sample frames */
    const uint64_t frames = (points / SND::deviceChannels);

    /* (sample frames * 1000000) / frequency == play length, in ms */
    return (frames * 1000000) / SND::deviceFrequency;
}

////////////////////////Pause///////////////////////////

void SNDPausePush()
{
	pause_level++;

	script3d.PauseAllPlayed(pause_level);
	script2d.PauseAllPlayed(pause_level);
}

void SNDPausePop()
{
	if (pause_level==0) return;

	script3d.PlayByLevel(pause_level);
	script2d.PlayByLevel(pause_level);

	pause_level--;
}

int SNDGetPushLevel()
{
	return pause_level;
}

