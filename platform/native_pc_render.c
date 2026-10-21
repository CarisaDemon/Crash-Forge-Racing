/* Crash Forge: Racing - "Modo PC" de dibujo, etapa 1: profundidad compartida.
 * Ver include/platform/native_pc_render.h para la explicacion general.
 *
 * Como funciona:
 * - MainFrame_RenderFrame marca, con el cursor de PrimMem, que tramos de la
 *   memoria de paquetes son mundo 3D solido (pista, modelos, ruedas).
 * - Al traducir cada paquete a OpenGL, native_gpu.c pregunta si el paquete
 *   cae en uno de esos tramos. Solo esos (y solo si son opacos, estan en
 *   pantalla y traen profundidad GTE en todos sus vertices) usan el Z-buffer.
 * - Todo lo demas se sigue pintando en el orden PS1 de siempre.
 *
 * Pruebas:
 * - Ctrl+Alt+Shift+P alterna Modo PC / clasico en vivo.
 * - Con debug/pc_render_ab.enable, durante el stress test se guardan pares
 *   de capturas a media resolucion (frame N clasico, frame N+1 PC) en
 *   debug/pc_ab_NNNN_clasico.ppm y debug/pc_ab_NNNN_pc.ppm.
 * - Cada 2000 frames el log informa que porcentaje del mundo solido uso Z.
 * - Con debug/pc_render.clasico el build de prueba arranca en modo clasico
 *   (lo usa la prueba automatica para medir FPS de los dos modos).
 */
#if defined(CTR_NATIVE) && defined(CTR_PC_RENDER_QA)

#define NPR_MAX_RANGES (64)
#define NPR_STATS_FRAMES (2000u)

typedef struct NprRange
{
	uintptr_t begin;
	uintptr_t end;
	u32 tags; /* diagnostico: que llamadas (1=karts/objetos, 2=ruedas, 4=pista) cubre */
} NprRange;

static NprRange s_nprRanges[NPR_MAX_RANGES];
static int s_nprRangeCount;
static uintptr_t s_nprOpen;
static u32 s_nprBeginCount;   /* diagnostico: orden de WorldBegin en el frame */
static int s_nprDiagChecked;
static int s_nprDiagEnabled;
static u32 s_nprDiagLines;
static int s_nprActive = 1;     /* el build de prueba arranca en Modo PC */
static int s_nprOverride = -1;  /* -1 nada, 0 clasico, 1 PC (capturas A/B) */
static int s_nprAbChecked;
static int s_nprAbEnabled;
static u32 s_nprMeasured;
static u32 s_nprCapturePoint;
static int s_nprCaptureIsPc;
static int s_nprLoggedStart;
static int s_nprDepthStatus = -1; /* -1 sin probar, 0 no hay, 1 hay */
static u32 s_nprStatFrames;
static u32 s_nprStatOverflow;
static u32 s_nprStatRangeSum;
static unsigned long long s_nprStatWorld;
static unsigned long long s_nprStatDepth;

static const u32 s_nprPoints[] = {240u, 800u, 1500u, 2500u, 3800u};

void NativeRenderer_PcRenderCaptureHalfPPM(const char *path);

static int NativePcRender_AbEnabled(void)
{
	if (!s_nprAbChecked)
	{
		FILE *f = fopen("debug/pc_render_ab.enable", "rb");
		s_nprAbEnabled = (f != NULL);
		if (f)
			fclose(f);
		s_nprAbChecked = 1;
		if (s_nprAbEnabled)
			Platform_Log("[PC RENDER] capturas A/B activas: frames 240/800/1500/2500/3800 del stress test\n");
	}
	return s_nprAbEnabled;
}

/* Diagnostico (solo build de prueba, con debug/pc_render_diag.enable):
 * en los frames de captura 240/241 y 1500/1501 anota cada parte dibujada
 * con su profundidad, para ver por que algo desaparece en Modo PC. */
int NativePcRender_DiagActive(void)
{
	if (!s_nprDiagChecked)
	{
		FILE *f = fopen("debug/pc_render_diag.enable", "rb");
		s_nprDiagEnabled = (f != NULL);
		if (f)
			fclose(f);
		s_nprDiagChecked = 1;
	}
	return s_nprDiagEnabled &&
	       ((s_nprCapturePoint == 240u) || (s_nprCapturePoint == 1500u)) &&
	       (s_nprDiagLines < 1500u);
}

u32 NativePcRender_DiagFrameId(void)
{
	return s_nprCapturePoint * 10u + (u32)s_nprCaptureIsPc;
}

void NativePcRender_DiagCountLine(void)
{
	s_nprDiagLines++;
}

u32 NativePcRender_PacketRangeTags(const void *packet)
{
	const uintptr_t p = (uintptr_t)packet;

	for (int i = 0; i < s_nprRangeCount; i++)
	{
		if ((p >= s_nprRanges[i].begin) && (p < s_nprRanges[i].end))
			return s_nprRanges[i].tags;
	}
	return 0;
}

void NativePcRender_BeginFrame(void)
{
	s_nprRangeCount = 0;
	s_nprOpen = 0;
	s_nprBeginCount = 0;
	s_nprOverride = -1;
	s_nprCapturePoint = 0;
	s_nprCaptureIsPc = 0;

	if (!s_nprLoggedStart)
	{
		/* debug/pc_render.clasico: arrancar en modo clasico (pruebas automaticas A/B de FPS) */
		FILE *classic = fopen("debug/pc_render.clasico", "rb");
		s_nprLoggedStart = 1;
		if (classic)
		{
			fclose(classic);
			s_nprActive = 0;
		}
		Platform_Log("[PC RENDER] build de prueba: Modo PC etapa 1 (profundidad compartida). "
		             "Modo inicial: %s. Ctrl+Alt+Shift+P cambia entre PC y clasico.\n",
		             s_nprActive ? "PC" : "CLASICO");
	}

	if (NativePcRender_AbEnabled() && NativePerf_IsStressTestMeasuring())
	{
		const u32 next = s_nprMeasured + 1u;
		for (size_t i = 0; i < sizeof(s_nprPoints) / sizeof(s_nprPoints[0]); i++)
		{
			if (next == s_nprPoints[i])
			{
				s_nprOverride = 0;
				s_nprCapturePoint = s_nprPoints[i];
				s_nprCaptureIsPc = 0;
			}
			else if (next == s_nprPoints[i] + 1u)
			{
				s_nprOverride = 1;
				s_nprCapturePoint = s_nprPoints[i];
				s_nprCaptureIsPc = 1;
			}
		}
	}
}

void NativePcRender_WorldBegin(const void *cursor)
{
	s_nprOpen = (uintptr_t)cursor;
	s_nprBeginCount++;
}

void NativePcRender_WorldEnd(const void *cursor)
{
	const uintptr_t end = (uintptr_t)cursor;

	const u32 tag = (s_nprBeginCount >= 1u && s_nprBeginCount <= 31u) ? (1u << (s_nprBeginCount - 1u)) : 0x80000000u;

	if ((s_nprOpen != 0) && (end > s_nprOpen))
	{
		if ((s_nprRangeCount > 0) && (s_nprRanges[s_nprRangeCount - 1].end == s_nprOpen))
		{
			/* tramo contiguo al anterior: se extiende */
			s_nprRanges[s_nprRangeCount - 1].end = end;
			s_nprRanges[s_nprRangeCount - 1].tags |= tag;
		}
		else if (s_nprRangeCount < NPR_MAX_RANGES)
		{
			s_nprRanges[s_nprRangeCount].begin = s_nprOpen;
			s_nprRanges[s_nprRangeCount].end = end;
			s_nprRanges[s_nprRangeCount].tags = tag;
			s_nprRangeCount++;
		}
		else
		{
			s_nprStatOverflow++;
		}
	}
	s_nprOpen = 0;
}

int NativePcRender_IsWorldPacket(const void *packet)
{
	const uintptr_t p = (uintptr_t)packet;

	for (int i = 0; i < s_nprRangeCount; i++)
	{
		if ((p >= s_nprRanges[i].begin) && (p < s_nprRanges[i].end))
			return 1;
	}
	return 0;
}

int NativePcRender_IsActive(void)
{
	return (s_nprOverride >= 0) ? s_nprOverride : s_nprActive;
}

void NativePcRender_SetActive(int enabled)
{
	s_nprActive = (enabled != 0);
	Platform_LogWarn("[PC RENDER] Modo de dibujo: %s (Ctrl+Alt+Shift+P)\n",
	                 s_nprActive ? "PC (profundidad real)" : "CLASICO (orden PS1)");
}

void NativePcRender_Toggle(void)
{
	NativePcRender_SetActive(!s_nprActive);
}

void NativePcRender_SetDepthStatus(int available)
{
	s_nprDepthStatus = (available != 0);
}

void NativePcRender_NoteSplit(int worldOpaque, int depthTested)
{
	if (worldOpaque)
		s_nprStatWorld++;
	if (depthTested)
		s_nprStatDepth++;
}

void NativePcRender_EndScene(void)
{
	if (NativePcRender_DiagActive())
	{
		for (int i = 0; i < s_nprRangeCount; i++)
		{
			Platform_Log("[PC DIAG] frame=%u tramo=%d etiquetas=%x inicio=%p bytes=%u\n",
			             (unsigned)NativePcRender_DiagFrameId(), i, (unsigned)s_nprRanges[i].tags,
			             (void *)s_nprRanges[i].begin,
			             (unsigned)(s_nprRanges[i].end - s_nprRanges[i].begin));
			s_nprDiagLines++;
		}
	}
	if (s_nprCapturePoint != 0)
	{
		char path[96];
		snprintf(path, sizeof(path), "debug/pc_ab_%04u_%s.ppm",
		         (unsigned)s_nprCapturePoint, s_nprCaptureIsPc ? "pc" : "clasico");
		NativeRenderer_PcRenderCaptureHalfPPM(path);
		s_nprCapturePoint = 0;
	}

	if (NativePerf_IsStressTestMeasuring())
		s_nprMeasured++;

	s_nprStatRangeSum += (u32)s_nprRangeCount;
	if (++s_nprStatFrames >= NPR_STATS_FRAMES)
	{
		const double pct = (s_nprStatWorld != 0)
		                       ? (100.0 * (double)s_nprStatDepth / (double)s_nprStatWorld)
		                       : 0.0;
		Platform_Log("[PC RENDER] modo=%s buffer_profundidad=%s | ultimos %u frames: partes solidas del mundo=%llu, "
		             "con profundidad=%llu (%.1f%%), tramos/frame=%.1f, desbordes=%u\n",
		             NativePcRender_IsActive() ? "PC" : "CLASICO",
		             (s_nprDepthStatus < 0) ? "?" : (s_nprDepthStatus ? "SI" : "NO"),
		             (unsigned)s_nprStatFrames, s_nprStatWorld, s_nprStatDepth, pct,
		             (double)s_nprStatRangeSum / (double)s_nprStatFrames,
		             (unsigned)s_nprStatOverflow);
		s_nprStatFrames = 0;
		s_nprStatOverflow = 0;
		s_nprStatRangeSum = 0;
		s_nprStatWorld = 0;
		s_nprStatDepth = 0;
	}
}

#endif
