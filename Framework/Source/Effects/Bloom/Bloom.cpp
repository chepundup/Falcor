/***************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
#include "Framework.h"
#include "Bloom.h"
#include "Graphics/Program/ProgramVars.h"
#include "API/RenderContext.h"
#include "Utils/Gui.h"

namespace Falcor
{
    Bloom::UniquePtr Bloom::create(float threshold, uint32_t kernelSize, float sigma)
    {
        return Bloom::UniquePtr(new Bloom(threshold, kernelSize, sigma));
    }

    Bloom::Bloom(float threshold, uint32_t kernelSize, float sigma)
    {
        mpBlur = GaussianBlur::create(kernelSize, sigma);
        mpBlitPass = FullScreenPass::create("Framework/Shaders/Blit.vs.slang", "Framework/Shaders/Blit.ps.slang");
        mSrcTexLoc = mpBlitPass->getProgram()->getReflector()->getDefaultParameterBlock()->getResourceBinding("gTex");
        mpVars = GraphicsVars::create(mpBlitPass->getProgram()->getReflector());
        mpVars["SrcRectCB"]["gOffset"] = vec2(0.0f);
        mpVars["SrcRectCB"]["gScale"] = vec2(1.0f);

        BlendState::Desc desc;
        desc.setRtBlend(0, true);
        desc.setRtParams(0, 
            BlendState::BlendOp::Add, BlendState::BlendOp::Add, 
            BlendState::BlendFunc::One, BlendState::BlendFunc::One, 
            BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha);

        mpAdditiveBlend = BlendState::create(desc);

        Sampler::Desc samplerDesc;
        samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
        mpSampler = Sampler::create(samplerDesc);
        mpVars->setSampler("gSampler", mpSampler);

        mpFilter = PassFilter::create(PassFilter::Type::HighPass, threshold);
        mpFilterResultFbo = Fbo::create();
    }

    void Bloom::updateLowResTexture(const Texture::SharedPtr& pTexture)
    {
        // Create FBO if not created already, or properties have changed since last use
        bool createFbo = mpLowResTexture == nullptr;

        float aspectRatio = (float)pTexture->getWidth() / (float)pTexture->getHeight();
        uint32_t lowResHeight = max(pTexture->getHeight() / 4, 256u);
        uint32_t lowResWidth = max(pTexture->getWidth() / 4, (uint32_t)(256.0f * aspectRatio));

        if (createFbo == false)
        {
            createFbo = (lowResWidth != mpLowResTexture->getWidth()) ||
                (lowResHeight != mpLowResTexture->getHeight()) ||
                (pTexture->getFormat() != mpLowResTexture->getFormat());
        }

        if (createFbo)
        {
            mpLowResTexture = Texture::create2D(
                lowResWidth,
                lowResHeight,
                pTexture->getFormat(), 
                1, 
                1, 
                nullptr, 
                Resource::BindFlags::ShaderResource | Resource::BindFlags::RenderTarget);
        }
    }

    void Bloom::execute(RenderContext* pRenderContext, Fbo::SharedPtr pFbo)
    {
        updateLowResTexture(pFbo->getColorTexture(0));

        pRenderContext->blit(pFbo->getColorTexture(0)->getSRV(), mpLowResTexture->getRTV());

        // Run high-pass filter and attach it to an FBO for blurring
        Texture::SharedPtr pHighPassResult = mpFilter->execute(pRenderContext, mpLowResTexture);
        mpFilterResultFbo->attachColorTarget(pHighPassResult, 0);
        mpBlur->execute(pRenderContext, pHighPassResult, mpFilterResultFbo);

        // Execute bloom
        mpVars->getDefaultBlock()->setSrv(mSrcTexLoc, 0, pHighPassResult->getSRV());
        GraphicsState::SharedPtr pState = pRenderContext->getGraphicsState();
        pState->pushFbo(pFbo);
        pRenderContext->pushGraphicsVars(mpVars);

        mpBlitPass->execute(pRenderContext, nullptr, mpAdditiveBlend);

        pRenderContext->popGraphicsVars();
        pState->popFbo();
    }

    void Bloom::renderUI(Gui* pGui, const char* uiGroup)
    {
        if (uiGroup == nullptr || pGui->beginGroup(uiGroup))
        {
            float threshold = mpFilter->getThreshold();
            if (pGui->addFloatVar("Threshold", threshold, 0.0f))
            {
                mpFilter->setThreshold(threshold);
            }

            int32_t kernelWidth = mpBlur->getKernelWidth();
            if (pGui->addIntVar("Kernel Width", (int&)kernelWidth, 1, 15, 2))
            {
                mpBlur->setKernelWidth(kernelWidth);
            }

            float sigma = mpBlur->getSigma();
            if (pGui->addFloatVar("Sigma", sigma, 0.001f))
            {
                mpBlur->setSigma(sigma);
            }

            if (uiGroup) 
            {
                pGui->endGroup();
            }
        }
    }

}
