/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "LuaTextures.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/FBO.h"
#include "System/SpringMath.h"
#include "System/StringUtil.h"
#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "Lua/LuaFBOs.h"

#include "fmt/format.h"


const spring::unordered_map<GLenum, GLenum> LuaTextures::Format2Query =
{
	{ GL_TEXTURE_1D                  , GL_TEXTURE_BINDING_1D				   },
	{ GL_TEXTURE_2D                  , GL_TEXTURE_BINDING_2D				   },
	{ GL_TEXTURE_3D                  , GL_TEXTURE_BINDING_3D				   },
//	{ GL_TEXTURE_1D_ARRAY            , GL_TEXTURE_BINDING_1D_ARRAY			   },
//	{ GL_TEXTURE_2D_ARRAY            , GL_TEXTURE_BINDING_2D_ARRAY			   },
//	{ GL_TEXTURE_RECTANGLE           , GL_TEXTURE_BINDING_RECTANGLE			   },
	{ GL_TEXTURE_CUBE_MAP            , GL_TEXTURE_BINDING_CUBE_MAP			   },
//	{ GL_TEXTURE_BUFFER              , GL_TEXTURE_BINDING_BUFFER			   },
	{ GL_TEXTURE_2D_MULTISAMPLE      , GL_TEXTURE_BINDING_2D_MULTISAMPLE	   },
//	{ GL_TEXTURE_2D_MULTISAMPLE_ARRAY, GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY },
};

/******************************************************************************/
/******************************************************************************/

std::string LuaTextures::Create(const Texture& tex)
{
	GLint currentBinding = 0;

	if (Format2Query.find(tex.target) == Format2Query.end()) {
		LOG_L(L_ERROR, "[LuaTextures::%s] texture-target %d is not supported", __func__, tex.target);
		return "";
	}

	glGetIntegerv(Format2Query.find(tex.target)->second, &currentBinding);

	GLuint texID;
	glGenTextures(1, &texID);
	glBindTexture(tex.target, texID);

	glClearErrors("LuaTex", __func__, globalRendering->glDebugErrors);

	GLenum dataFormat = GL_RGBA;
	GLenum dataType   = GL_UNSIGNED_BYTE;

	switch (tex.format) {
		case GL_DEPTH_COMPONENT:
		case GL_DEPTH_COMPONENT16:
		case GL_DEPTH_COMPONENT24:
		case GL_DEPTH_COMPONENT32:
		case GL_DEPTH_COMPONENT32F: {
			dataFormat = GL_DEPTH_COMPONENT;
			dataType = GL_FLOAT;
		} break;
		default: {
		} break;
	}

	switch (tex.target) {
		case GL_TEXTURE_1D: {
			glTexImage1D(tex.target, 0, tex.format, tex.xsize                      , tex.border, dataFormat, dataType, nullptr);
		} break;
		case GL_TEXTURE_2D: {
			glTexImage2D(tex.target, 0, tex.format, tex.xsize, tex.ysize           , tex.border, dataFormat, dataType, nullptr);
		} break;
		case GL_TEXTURE_3D: {
			glTexImage3D(tex.target, 0, tex.format, tex.xsize, tex.ysize, tex.zsize, tex.border, dataFormat, dataType, nullptr);
		} break;

		case GL_TEXTURE_2D_MULTISAMPLE: {
			assert(tex.samples > 1);

			// 2DMS target only makes sense for FBO's
			if (!globalRendering->supportMSAAFrameBuffer) {
				glDeleteTextures(1, &texID);
				glBindTexture(tex.target, currentBinding);
				return "";
			}

			glTexImage2DMultisample(tex.target, tex.samples, tex.format, tex.xsize, tex.ysize, GL_TRUE);
		} break;

		default: {
			assert(false);
		} break;
	}

	if (glGetError() != GL_NO_ERROR) {
		glDeleteTextures(1, &texID);
		glBindTexture(tex.target, currentBinding);
		return "";
	}

	ApplyParams(tex);

	glBindTexture(tex.target, currentBinding); // revert the current binding

	GLuint fbo = 0;
	GLuint fboDepth = 0;

	if (tex.fbo != 0) {
		if (!FBO::IsSupported()) {
			glDeleteTextures(1, &texID);
			return "";
		}

		GLint currentFBO;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);

		glGenFramebuffersEXT(1, &fbo);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

		if (tex.fboDepth != 0) {
			glGenRenderbuffersEXT(1, &fboDepth);
			glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, fboDepth);
			GLenum depthFormat = static_cast<GLenum>(CGlobalRendering::DepthBitsToFormat(globalRendering->supportDepthBufferBitDepth));
			glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, depthFormat, tex.xsize, tex.ysize);
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fboDepth);
		}

		bool attachFailure = false;
		try {
			LuaFBOs::AttachObjectTexTarget(__func__, GL_FRAMEBUFFER_EXT, tex.target, texID, GL_COLOR_ATTACHMENT0_EXT, 0);
		}
		catch (const opengl_error& e) {
			attachFailure = true;
			LOG_L(L_ERROR, "[LuaTextures::%s] %s", __func__, e.what());
		}

		if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT || attachFailure) {
			glDeleteTextures(1, &texID);
			glDeleteFramebuffersEXT(1, &fbo);
			glDeleteRenderbuffersEXT(1, &fboDepth);
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, currentFBO);
			return "";
		}

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, currentFBO);
	}

	std::string str = fmt::format("{}{}", prefix, ++lastCode);

	Texture newTex = tex;
	newTex.id = texID;
	newTex.fbo = fbo;
	newTex.fboDepth = fboDepth;

	if (freeIndices.empty()) {
		textureMap.insert(str, textureVec.size());
		textureVec.emplace_back(newTex);
		return str;
	}

	// recycle
	textureMap[str] = freeIndices.back();
	textureVec[freeIndices.back()] = newTex;
	freeIndices.pop_back();
	return str;
}


bool LuaTextures::Bind(const std::string& name) const
{
	const auto it = textureMap.find(name);

	if (it != textureMap.end()) {
		const Texture& tex = textureVec[it->second];
		glBindTexture(tex.target, tex.id);
		return true;
	}

	return false;
}


bool LuaTextures::Free(const std::string& name)
{
	const auto it = textureMap.find(name);

	if (it != textureMap.end()) {
		const Texture& tex = textureVec[it->second];
		glDeleteTextures(1, &tex.id);

		if (FBO::IsSupported()) {
			glDeleteFramebuffersEXT(1, &tex.fbo);
			glDeleteRenderbuffersEXT(1, &tex.fboDepth);
		}

		freeIndices.push_back(it->second);
		textureMap.erase(it);
		return true;
	}

	return false;
}


bool LuaTextures::FreeFBO(const std::string& name)
{
	if (!FBO::IsSupported())
		return false;

	const auto it = textureMap.find(name);

	if (it == textureMap.end())
		return false;

	Texture& tex = textureVec[it->second];

	glDeleteFramebuffersEXT(1, &tex.fbo);
	glDeleteRenderbuffersEXT(1, &tex.fboDepth);

	tex.fbo = 0;
	tex.fboDepth = 0;
	return true;
}


void LuaTextures::FreeAll()
{
	for (const auto& item: textureMap) {
		const Texture& tex = textureVec[item.second];
		glDeleteTextures(1, &tex.id);

		if (FBO::IsSupported()) {
			glDeleteFramebuffersEXT(1, &tex.fbo);
			glDeleteRenderbuffersEXT(1, &tex.fboDepth);
		}
	}

	textureMap.clear();
	textureVec.clear();
	freeIndices.clear();
}


void LuaTextures::ApplyParams(const Texture& tex) const
{
	glTexParameteri(tex.target, GL_TEXTURE_WRAP_S, tex.wrap_s);
	glTexParameteri(tex.target, GL_TEXTURE_WRAP_T, tex.wrap_t);
	glTexParameteri(tex.target, GL_TEXTURE_WRAP_R, tex.wrap_r);
	glTexParameteri(tex.target, GL_TEXTURE_MIN_FILTER, tex.min_filter);
	glTexParameteri(tex.target, GL_TEXTURE_MAG_FILTER, tex.mag_filter);
	if (tex.cmpFunc != GL_NONE) {
		glTexParameteri(tex.target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(tex.target, GL_TEXTURE_COMPARE_FUNC, tex.cmpFunc);
	}
	else {
		glTexParameteri(tex.target, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		glTexParameteri(tex.target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL); //sensible default
	}

	if (tex.lodBias != 0.0f)
		glTexParameterf(tex.target, GL_TEXTURE_LOD_BIAS, tex.lodBias);

	if (tex.aniso != 0.0f && GLEW_EXT_texture_filter_anisotropic)
		glTexParameterf(tex.target, GL_TEXTURE_MAX_ANISOTROPY_EXT, Clamp(tex.aniso, 1.0f, globalRendering->maxTexAnisoLvl));
}

void LuaTextures::ChangeParams(const Texture& tex)  const
{
	GLint currentBinding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &currentBinding);
	glBindTexture(tex.target, tex.id);
	ApplyParams(tex);
	glBindTexture(GL_TEXTURE_2D, currentBinding); // revert the current binding
}


size_t LuaTextures::GetIdx(const std::string& name) const
{
	const auto it = textureMap.find(name);

	if (it != textureMap.end())
		return (it->second);

	return (size_t(-1));
}

