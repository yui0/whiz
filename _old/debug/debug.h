//---------------------------------------------------------
//	Debug for Whiz (Japanese Input Method Engine)
//
//		(C)2003 NAKADA
//---------------------------------------------------------

#ifdef DEBUG
	#define debug( whizdebug )	 whizdebug
#else
	#define debug( whizdebug )
#endif

#define D_ERR	0	// エラー
#define D_WARN	1	// 警告
#define D_INFO	5	// インフォメーション

#ifdef DEBUG
	void dmsg(int lev, const char *f, ...);
#endif

void smsg(int lev, const char *f, ...);
