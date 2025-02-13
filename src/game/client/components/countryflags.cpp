/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include "countryflags.h"

#include <game/client/render.h>

void CCountryFlags::LoadCountryflagsIndexfile()
{
	IOHANDLE File = Storage()->OpenFile("countryflags/index.txt", IOFLAG_READ | IOFLAG_SKIP_BOM, IStorage::TYPE_ALL);
	if(!File)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "countryflags", "couldn't open index file");
		return;
	}

	char aOrigin[128];
	CLineReader LineReader;
	LineReader.Init(File);
	char *pLine;
	while((pLine = LineReader.Get()))
	{
		if(!str_length(pLine) || pLine[0] == '#') // skip empty lines and comments
			continue;

		str_copy(aOrigin, pLine, sizeof(aOrigin));
		char *pReplacement = LineReader.Get();
		if(!pReplacement)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "countryflags", "unexpected end of index file");
			break;
		}

		if(pReplacement[0] != '=' || pReplacement[1] != '=' || pReplacement[2] != ' ')
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "malform replacement for index '%s'", aOrigin);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "countryflags", aBuf);
			continue;
		}

		int CountryCode = str_toint(pReplacement + 3);
		if(CountryCode < CODE_LB || CountryCode > CODE_UB)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "country code '%i' not within valid code range [%i..%i]", CountryCode, CODE_LB, CODE_UB);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "countryflags", aBuf);
			continue;
		}

		// load the graphic file
		char aBuf[128];
		CImageInfo Info;
		str_format(aBuf, sizeof(aBuf), "countryflags/%s.png", aOrigin);
		if(!Graphics()->LoadPNG(&Info, aBuf, IStorage::TYPE_ALL))
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "failed to load '%s'", aBuf);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "countryflags", aMsg);
			continue;
		}

		// add entry
		CCountryFlag CountryFlag;
		CountryFlag.m_CountryCode = CountryCode;
		str_copy(CountryFlag.m_aCountryCodeString, aOrigin, sizeof(CountryFlag.m_aCountryCodeString));
		CountryFlag.m_Texture = Graphics()->LoadTextureRaw(Info.m_Width, Info.m_Height, Info.m_Format, Info.m_pData, Info.m_Format, 0, aOrigin);
		Graphics()->FreePNG(&Info);

		if(g_Config.m_Debug)
		{
			str_format(aBuf, sizeof(aBuf), "loaded country flag '%s'", aOrigin);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "countryflags", aBuf);
		}
		m_aCountryFlags.push_back(CountryFlag);
	}
	io_close(File);
	std::sort(m_aCountryFlags.begin(), m_aCountryFlags.end());

	// find index of default item
	size_t DefaultIndex = 0;
	for(size_t Index = 0; Index < m_aCountryFlags.size(); ++Index)
		if(m_aCountryFlags[Index].m_CountryCode == -1)
		{
			DefaultIndex = Index;
			break;
		}

	// init LUT
	if(DefaultIndex != 0)
		for(size_t &CodeIndexLUT : m_CodeIndexLUT)
			CodeIndexLUT = DefaultIndex;
	else
		mem_zero(m_CodeIndexLUT, sizeof(m_CodeIndexLUT));
	for(size_t i = 0; i < m_aCountryFlags.size(); ++i)
		m_CodeIndexLUT[maximum(0, (m_aCountryFlags[i].m_CountryCode - CODE_LB) % CODE_RANGE)] = i;
}

void CCountryFlags::OnInit()
{
	// load country flags
	m_aCountryFlags.clear();
	LoadCountryflagsIndexfile();
	if(m_aCountryFlags.empty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "countryflags", "failed to load country flags. folder='countryflags/'");
		CCountryFlag DummyEntry;
		DummyEntry.m_CountryCode = -1;
		mem_zero(DummyEntry.m_aCountryCodeString, sizeof(DummyEntry.m_aCountryCodeString));
		m_aCountryFlags.push_back(DummyEntry);
	}

	m_FlagsQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	RenderTools()->QuadContainerAddSprite(m_FlagsQuadContainerIndex, 0, 0, 1, 1);
	Graphics()->QuadContainerUpload(m_FlagsQuadContainerIndex);
}

size_t CCountryFlags::Num() const
{
	return m_aCountryFlags.size();
}

const CCountryFlags::CCountryFlag *CCountryFlags::GetByCountryCode(int CountryCode) const
{
	return GetByIndex(m_CodeIndexLUT[maximum(0, (CountryCode - CODE_LB) % CODE_RANGE)]);
}

const CCountryFlags::CCountryFlag *CCountryFlags::GetByIndex(size_t Index) const
{
	return &m_aCountryFlags[Index % m_aCountryFlags.size()];
}

void CCountryFlags::Render(int CountryCode, const ColorRGBA *pColor, float x, float y, float w, float h)
{
	const CCountryFlag *pFlag = GetByCountryCode(CountryCode);
	if(pFlag->m_Texture.IsValid())
	{
		Graphics()->TextureSet(pFlag->m_Texture);
		Graphics()->SetColor(*pColor);
		Graphics()->RenderQuadContainerEx(m_FlagsQuadContainerIndex, 0, -1, x, y, w, h);
	}
}
