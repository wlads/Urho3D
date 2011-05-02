//
// Urho3D Engine
// Copyright (c) 2008-2011 Lasse ��rni
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "AnimatedModel.h"
#include "Animation.h"
#include "AnimationController.h"
#include "AnimationState.h"
#include "Context.h"
#include "MemoryBuffer.h"
#include "Profiler.h"
#include "ResourceCache.h"
#include "Scene.h"
#include "SceneEvents.h"
#include "VectorBuffer.h"

#include "DebugNew.h"

static std::string noBoneName;

OBJECTTYPESTATIC(AnimationController);

AnimationController::AnimationController(Context* context) :
    Component(context)
{
}

AnimationController::~AnimationController()
{
}

void AnimationController::RegisterObject(Context* context)
{
    context->RegisterFactory<AnimationController>();
    
    ATTRIBUTE(AnimationController, VAR_BUFFER, "Animations", animations_, std::vector<unsigned char>());
}


void AnimationController::OnSetAttribute(const AttributeInfo& attr, const Variant& value)
{
    switch (attr.offset_)
    {
    case offsetof(AnimationController, animations_):
        {
            MemoryBuffer buf(value.GetBuffer());
            animations_.resize(buf.ReadVLE());
            for (std::vector<AnimationControl>::iterator i = animations_.begin(); i != animations_.end(); ++i)
            {
                i->hash_ = buf.ReadStringHash();
                i->group_ = buf.ReadUByte();
                i->speed_ = buf.ReadFloat();
                i->targetWeight_ = buf.ReadFloat();
                i->fadeTime_ = buf.ReadFloat();
                i->autoFadeTime_ = buf.ReadFloat();
            }
        }
        break;
        
    default:
        Serializable::OnSetAttribute(attr, value);
        break;
    }
}

Variant AnimationController::OnGetAttribute(const AttributeInfo& attr)
{
    switch (attr.offset_)
    {
    case offsetof(AnimationController, animations_):
        {
            VectorBuffer buf;
            buf.WriteVLE(animations_.size());
            for (std::vector<AnimationControl>::const_iterator i = animations_.begin(); i != animations_.end(); ++i)
            {
                buf.WriteStringHash(i->hash_);
                buf.WriteUByte(i->group_);
                buf.WriteFloat(i->speed_);
                buf.WriteFloat(i->targetWeight_);
                buf.WriteFloat(i->fadeTime_);
                buf.WriteFloat(i->autoFadeTime_);
            }
            return buf.GetBuffer();
        }
        
    default:
        return Serializable::OnGetAttribute(attr);
    }
}

void AnimationController::Update(float timeStep)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return;
    
    PROFILE(UpdateAnimationController);
    
    // Loop through animations
    for (std::vector<AnimationControl>::iterator i = animations_.begin(); i != animations_.end();)
    {
        bool remove = false;
        AnimationState* state = model->GetAnimationState(i->hash_);
        if (!state)
            remove = true;
        else
        {
            // Advance the animation
            if (i->speed_ != 0.0f)
                state->AddTime(i->speed_ * timeStep);
            
            float targetWeight = i->targetWeight_;
            float fadeTime = i->fadeTime_;
            
            // If non-looped animation at the end, activate autofade as applicable
            if ((!state->IsLooped()) && (state->GetTime() >= state->GetLength()) && (i->autoFadeTime_ > 0.0f))
            {
                targetWeight = 0.0f;
                fadeTime = i->autoFadeTime_;
            }
            
            // Process weight fade
            float currentWeight = state->GetWeight();
            if ((currentWeight != targetWeight) && (fadeTime > 0.0f))
            {
                float weightDelta = 1.0f / fadeTime * timeStep;
                if (currentWeight < targetWeight)
                    currentWeight = Min(currentWeight + weightDelta, targetWeight);
                else if (currentWeight > targetWeight)
                    currentWeight = Max(currentWeight - weightDelta, targetWeight);
                state->SetWeight(currentWeight);
            }
            
            // Remove if weight zero and target weight zero
            if ((state->GetWeight() == 0.0f) && ((targetWeight == 0.0f) || (fadeTime == 0.0f)))
                remove = true;
        }
        
        if (remove)
        {
            if (state)
                model->RemoveAnimationState(state);
            i = animations_.erase(i);
        }
        else
            ++i;
    }
}

bool AnimationController::AddAnimation(const std::string& name, unsigned char group)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return false;
    
    // Check if already exists
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    
    if (!state)
    {
        Animation* newAnimation = GetSubsystem<ResourceCache>()->GetResource<Animation>(name);
        state = model->AddAnimationState(newAnimation);
        if (!state)
            return false;
    }
    
    if (index == M_MAX_UNSIGNED)
    {
        AnimationControl newControl;
        Animation* animation = state->GetAnimation();
        newControl.hash_ = animation->GetNameHash();
        animations_.push_back(newControl);
        index = animations_.size() - 1;
    }
    
    animations_[index].group_ = group;
    return true;
}

bool AnimationController::RemoveAnimation(const std::string& name, float fadeTime)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return false;
    
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (fadeTime <= 0.0f)
    {
        if (index != M_MAX_UNSIGNED)
            animations_.erase(animations_.begin() + index);
        if (state)
            model->RemoveAnimationState(state);
    }
    else
    {
        if (index != M_MAX_UNSIGNED)
        {
            animations_[index].targetWeight_ = 0.0f;
            animations_[index].fadeTime_ = fadeTime;
        }
    }
    
    return (index != M_MAX_UNSIGNED) || (state != 0);
}

void AnimationController::RemoveAnimations(unsigned char group, float fadeTime)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return;
    
    for (std::vector<AnimationControl>::iterator i = animations_.begin(); i != animations_.end();)
    {
        bool remove = false;
        
        if (i->group_ == group)
        {
            if (fadeTime < 0.0f)
            {
                remove = true;
                AnimationState* state = model->GetAnimationState(i->hash_);
                if (state)
                    model->RemoveAnimationState(state);
            }
            else
            {
                i->targetWeight_ = 0.0f;
                i->fadeTime_ = fadeTime;
            }
        }
        
        if (remove)
            i = animations_.erase(i);
        else
            ++i;
    }
}

void AnimationController::RemoveAllAnimations(float fadeTime)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return;
    
    for (std::vector<AnimationControl>::iterator i = animations_.begin(); i != animations_.end();)
    {
        bool remove = false;
        
        if (fadeTime < 0.0f)
        {
            remove = true;
            AnimationState* state = model->GetAnimationState(i->hash_);
            if (state)
                model->RemoveAnimationState(state);
        }
        else
        {
            i->targetWeight_ = 0.0f;
            i->fadeTime_ = fadeTime;
        }
    
        if (remove)
            i = animations_.erase(i);
        else
            ++i;
    }
}

bool AnimationController::SetAnimation(const std::string& name, unsigned char group, bool looped, bool restart, float speed,
    float targetWeight, float fadeTime, float autoFadeTime, bool fadeOutOthersInGroup)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if ((index == M_MAX_UNSIGNED) || (!state))
    {
        // If animation is not active, and target weight is zero, do nothing
        if (targetWeight <= 0.0f)
            return true;
        // Attempt to add, then re-find
        if (!AddAnimation(name, group))
            return false;
        FindAnimation(name, index, state);
    }
    
    state->SetLooped(looped);
    if (restart)
        state->SetTime(0.0f);
    
    AnimationControl& control = animations_[index];
    control.group_ = group;
    control.speed_ = speed;
    
    if (fadeTime > 0.0f)
        control.targetWeight_ = Clamp(targetWeight, 0.0f, 1.0f);
    else
        state->SetWeight(targetWeight);
    control.fadeTime_ = Max(fadeTime, 0.0f);
    control.autoFadeTime_ = Max(autoFadeTime, 0.0f);
    
    if (fadeOutOthersInGroup)
    {
        for (unsigned i = 0; i < animations_.size(); ++i)
        {
            AnimationControl& otherControl = animations_[i];
            if ((otherControl.group_ == group) && (i != index))
            {
                otherControl.targetWeight_ = 0.0f;
                otherControl.fadeTime_ = Max(fadeTime, M_EPSILON);
            }
        }
    }
    return true;
}

bool AnimationController::SetProperties(const std::string& name, unsigned char group, float speed, float targetWeight, float fadeTime,
    float autoFadeTime)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return false;
    AnimationControl& control = animations_[index];
    control.group_ = group;
    control.speed_ = speed;
    control.targetWeight_ = Clamp(targetWeight, 0.0f, 1.0f);
    control.fadeTime_ = Max(fadeTime, 0.0f);
    control.autoFadeTime_ = Max(fadeTime, 0.0f);
    return true;
}

bool AnimationController::SetPriority(const std::string& name, int priority)
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return false;
    state->SetPriority(priority);
    return true;
}

bool AnimationController::SetStartBone(const std::string& name, const std::string& startBoneName)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return false;
    
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return false;
    Bone* bone = model->GetSkeleton().GetBone(startBoneName);
    state->SetStartBone(bone);
    return true;
}

bool AnimationController::SetBlending(const std::string& name, int priority, const std::string& startBoneName)
{
    AnimatedModel* model = GetAnimatedModel();
    if (!model)
        return false;
    
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return false;
    Bone* bone = model->GetSkeleton().GetBone(startBoneName);
    state->SetPriority(priority);
    state->SetStartBone(bone);
    return true;
}

bool AnimationController::SetTime(const std::string& name, float time)
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return false;
    state->SetTime(time);
    return true;
}

bool AnimationController::SetGroup(const std::string& name, unsigned char group)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return false;
    animations_[index].group_ = group;
    return true;
}

bool AnimationController::SetSpeed(const std::string& name, float speed)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return false;
    animations_[index].speed_ = speed;
    return true;
}

bool AnimationController::SetWeight(const std::string& name, float weight)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if ((index == M_MAX_UNSIGNED) || (!state))
        return false;
    state->SetWeight(weight);
    // Stop any ongoing fade
    animations_[index].fadeTime_ = 0.0f;
    return true;
}

bool AnimationController::SetFade(const std::string& name, float targetWeight, float time)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return false;
    animations_[index].targetWeight_ = Clamp(targetWeight, 0.0f, 1.0f);
    animations_[index].fadeTime_ = Max(time, M_EPSILON);
    return true;
}

bool AnimationController::SetFadeOthers(const std::string& name, float targetWeight, float time)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return false;
    unsigned char group = animations_[index].group_;
    
    for (unsigned i = 0; i < animations_.size(); ++i)
    {
        AnimationControl& control = animations_[i];
        if ((control.group_ == group) && (i != index))
        {
            control.targetWeight_ = Clamp(targetWeight, 0.0f, 1.0f);
            control.fadeTime_ = Max(time, M_EPSILON);
        }
    }
    return true;
}

bool AnimationController::SetLooped(const std::string& name, bool enable)
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return false;
    state->SetLooped(enable);
    return true;
}

bool AnimationController::SetAutoFade(const std::string& name, float time)
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return false;
    animations_[index].autoFadeTime_ = Max(time, 0.0f);
    return true;
}

AnimatedModel* AnimationController::GetAnimatedModel() const
{
    return GetComponent<AnimatedModel>();
}

bool AnimationController::HasAnimation(const std::string& name) const
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    return index != M_MAX_UNSIGNED;
}

int AnimationController::GetPriority(const std::string& name) const
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return 0;
    return state->GetPriority();
}

Bone* AnimationController::GetStartBone(const std::string& name) const
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return 0;
    return state->GetStartBone();
}

const std::string& AnimationController::GetStartBoneName(const std::string& name) const
{
    Bone* bone = GetStartBone(name);
    return bone ? bone->name_ : noBoneName;
}

float AnimationController::GetTime(const std::string& name) const
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return 0.0f;
    return state->GetTime();
}

float AnimationController::GetWeight(const std::string& name) const
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return 0.0f;
    return state->GetWeight();
}

bool AnimationController::IsLooped(const std::string& name) const
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return false;
    return state->IsLooped();
}

float AnimationController::GetLength(const std::string& name) const
{
    AnimationState* state = FindAnimationState(name);
    if (!state)
        return 0.0f;
    return state->GetLength();
}

unsigned char AnimationController::GetGroup(const std::string& name) const
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return 0;
    return animations_[index].group_;
}

float AnimationController::GetSpeed(const std::string& name) const
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return 0.0f;
    return animations_[index].speed_;
}

float AnimationController::GetFadeTarget(const std::string& name) const
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return 0.0f;
    return animations_[index].targetWeight_;
}

float AnimationController::GetFadeTime(const std::string& name) const
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return 0.0f;
    return animations_[index].targetWeight_;
}

float AnimationController::GetAutoFade(const std::string& name) const
{
    unsigned index;
    AnimationState* state;
    FindAnimation(name, index, state);
    if (index == M_MAX_UNSIGNED)
        return 0.0f;
    return animations_[index].autoFadeTime_;
}

void AnimationController::OnNodeSet(Node* node)
{
    if (node)
    {
        Scene* scene = node->GetScene();
        if (scene)
            SubscribeToEvent(scene, E_SCENEPOSTUPDATE, HANDLER(AnimationController, HandleScenePostUpdate));
    }
}

void AnimationController::FindAnimation(const std::string& name, unsigned& index, AnimationState*& state) const
{
    AnimatedModel* model = GetAnimatedModel();
    StringHash nameHash(name);
    
    // Find the AnimationState
    state = model ? model->GetAnimationState(nameHash) : 0;
    if (state)
    {
        // Either a resource name or animation name may be specified. We store resource names, so correct the hash if necessary
        nameHash = state->GetAnimation()->GetNameHash();
    }
    
    // Find the internal control structure
    index = M_MAX_UNSIGNED;
    for (unsigned i = 0; i < animations_.size(); ++i)
    {
        if (animations_[i].hash_ == nameHash)
        {
            index = i;
            break;
        }
    }
}

AnimationState* AnimationController::FindAnimationState(const std::string& name) const
{
    AnimatedModel* model = GetAnimatedModel();
    StringHash nameHash(name);
    
    return model ? model->GetAnimationState(nameHash) : 0;
}

void AnimationController::HandleScenePostUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace ScenePostUpdate;
    
    Update(eventData[P_TIMESTEP].GetFloat());
}
