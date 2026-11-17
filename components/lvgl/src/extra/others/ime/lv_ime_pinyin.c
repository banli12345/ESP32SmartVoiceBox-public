/**
 * @file lv_ime_pinyin.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_ime_pinyin.h"
#if LV_USE_IME_PINYIN != 0

#include <stdio.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS    &lv_ime_pinyin_class

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_ime_pinyin_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_ime_pinyin_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_ime_pinyin_style_change_event(lv_event_t * e);
static void lv_ime_pinyin_kb_event(lv_event_t * e);
static void lv_ime_pinyin_cand_panel_event(lv_event_t * e);

static void init_pinyin_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict);
static void pinyin_input_proc(lv_obj_t * obj);
static void pinyin_page_proc(lv_obj_t * obj, uint16_t btn);
static char * pinyin_search_matching(lv_obj_t * obj, char * py_str, uint16_t * cand_num);
static void pinyin_ime_clear_data(lv_obj_t * obj);
static void pinyin_render_candidates(lv_obj_t * obj);
static void pinyin_set_expanded(lv_obj_t * obj, bool expanded);
static void pinyin_language_event(lv_event_t * e);
static void pinyin_sync_language(lv_obj_t * obj);

#if LV_IME_PINYIN_USE_K9_MODE
    static void pinyin_k9_init_data(lv_obj_t * obj);
    static void pinyin_k9_get_legal_py(lv_obj_t * obj, char * k9_input, const char * py9_map[]);
    static bool pinyin_k9_is_valid_py(lv_obj_t * obj, char * py_str);
    static void pinyin_k9_fill_cand(lv_obj_t * obj);
    static void pinyin_k9_cand_page_proc(lv_obj_t * obj, uint16_t dir);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_ime_pinyin_class = {
    .constructor_cb = lv_ime_pinyin_constructor,
    .destructor_cb  = lv_ime_pinyin_destructor,
    .width_def      = LV_SIZE_CONTENT,
    .height_def     = LV_SIZE_CONTENT,
    .group_def      = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size  = sizeof(lv_ime_pinyin_t),
    .base_class     = &lv_obj_class
};

#if LV_IME_PINYIN_USE_K9_MODE
static const char * lv_btnm_def_pinyin_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 20] = {\
                                                                                ",\0", "1#\0",  "abc \0", "def\0",  LV_SYMBOL_BACKSPACE"\0", "\n\0",
                                                                                ".\0", "ghi\0", "jkl\0", "mno\0",  LV_SYMBOL_KEYBOARD"\0", "\n\0",
                                                                                "?\0", "pqrs\0", "tuv\0", "wxyz\0",  LV_SYMBOL_NEW_LINE"\0", "\n\0",
                                                                                LV_SYMBOL_LEFT"\0", "\0"
                                                                               };

static lv_btnmatrix_ctrl_t default_kb_ctrl_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 16] = { 1 };
static char   lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 2][LV_IME_PINYIN_K9_MAX_INPUT] = {0};
#endif



#if LV_IME_PINYIN_USE_DEFAULT_DICT
lv_pinyin_dict_t lv_ime_pinyin_def_dict[] = {
    { "a", "啊" },
    { "ai", "愛" },
    { "an", "安暗案" },
    { "ba", "吧把爸八" },
    { "bai", "百白敗" },
    { "ban", "半般辦" },
    { "bang", "旁" },
    { "bao", "保薄包報" },
    { "bei", "被背悲北杯備" },
    { "ben", "本" },
    { "bi", "必比避鼻彼筆秘閉" },
    { "bian", "便邊變変辺" },
    { "biao", "表標" },
    { "bie", "別" },
    { "bing", "病並氷" },
    { "bo", "波薄泊" },
    { "bu", "不布步部捕補歩" },
    { "ca", "察" },
    { "cai", "才材菜財採" },
    { "can", "参残參" },
    { "ce", "策側" },
    { "ceng", "曾" },
    { "cha", "差查茶" },
    { "chai", "差" },
    { "chan", "產産單" },
    { "chang", "場廠" },
    { "chao", "超朝" },
    { "che", "車" },
    { "cheng", "成程乗" },
    { "chi", "尺吃持赤池遅歯" },
    { "chong", "充种重種" },
    { "chu", "出初楚触處処" },
    { "chuan", "川船傳" },
    { "chuang", "創窓" },
    { "chun", "春" },
    { "ci", "此次辞差" },
    { "cong", "從従" },
    { "cu", "卒" },
    { "cun", "存村" },
    { "cuo", "錯" },
    { "da", "大打答達" },
    { "dai", "代待帯帶貸" },
    { "dan", "但担擔誕單単" },
    { "dang", "当党當黨" },
    { "dao", "到道盗導島辺" },
    { "de", "的得" },
    { "dei", "" },
    { "deng", "等" },
    { "di", "地得低底弟第締" },
    { "dian", "点电店點電" },
    { "diao", "調" },
    { "ding", "定町" },
    { "dong", "冬東動働凍" },
    { "du", "独度都渡読" },
    { "duan", "段断短斷" },
    { "dui", "對対" },
    { "duo", "多駄" },
    { "e", "嗯悪" },
    { "en", "嗯" },
    { "er", "而耳二兒" },
    { "fa", "乏法發発髪" },
    { "fan", "反返犯番仮販飯範払" },
    { "fang", "方放房坊訪" },
    { "fei", "非飛費" },
    { "fen", "分份" },
    { "feng", "風豐" },
    { "fou", "否不" },
    { "fu", "父夫富服符付附府幅婦復複負払" },
    { "gai", "改概該" },
    { "gan", "甘感敢" },
    { "gang", "港剛" },
    { "gao", "告高" },
    { "ge", "各格歌革割個" },
    { "gei", "給" },
    { "gen", "跟根" },
    { "geng", "更" },
    { "gong", "工共供功公" },
    { "gou", "夠構溝" },
    { "gu", "古故鼓" },
    { "guai", "掛" },
    { "guan", "官管慣館觀関關" },
    { "guang", "光広" },
    { "gui", "規帰" },
    { "guo", "果国裏菓國過" },
    { "hai", "孩海害還" },
    { "han", "寒漢" },
    { "hang", "航行" },
    { "hao", "好号" },
    { "he", "合和喝何荷" },
    { "hei", "黒" },
    { "hen", "很" },
    { "heng", "行横" },
    { "hou", "厚喉候後" },
    { "hu", "乎呼湖護" },
    { "hua", "化画花話畫劃" },
    { "huai", "壊劃" },
    { "huan", "緩環歡還換" },
    { "huang", "黄" },
    { "hui", "回会慧絵揮會" },
    { "hun", "混婚" },
    { "huo", "活或火獲" },
    { "i", "" },
    { "ji", "己计及机既急季寄技即集基祭系奇紀積計記済幾際極繼績機濟" },
    { "jia", "家加價" },
    { "jian", "件建健肩見減間検簡漸" },
    { "jiang", "降強講將港" },
    { "jiao", "叫教交角覚覺較學" },
    { "jie", "介借接姐皆届界解結階節價" },
    { "jin", "今近禁金僅進" },
    { "jing", "京境景静精經経" },
    { "jiu", "就久九酒究" },
    { "ju", "句具局居決挙據舉" },
    { "jue", "角覚覺" },
    { "jun", "均" },
    { "kai", "開" },
    { "kan", "看刊" },
    { "kang", "康" },
    { "kao", "考" },
    { "ke", "可刻科克客渇課" },
    { "ken", "肯" },
    { "kong", "空控" },
    { "kou", "口" },
    { "ku", "苦庫" },
    { "kuai", "快塊会會" },
    { "kuang", "況" },
    { "kun", "困" },
    { "kuo", "括拡適" },
    { "la", "拉啦落" },
    { "lai", "来來頼" },
    { "lao", "老絡落" },
    { "le", "了楽樂" },
    { "lei", "類" },
    { "leng", "冷" },
    { "li", "力立利理例礼離麗裡勵歷" },
    { "lian", "連練臉聯" },
    { "liang", "良量涼兩両" },
    { "liao", "料" },
    { "lie", "列" },
    { "lin", "林隣賃" },
    { "ling", "另令領" },
    { "liu", "六留流" },
    { "lu", "律路録緑陸履慮" },
    { "lv", "旅" },
    { "lun", "輪論" },
    { "luo", "落絡" },
    { "ma", "媽嗎嘛" },
    { "mai", "買売" },
    { "man", "滿" },
    { "mang", "忙" },
    { "mao", "毛猫貿" },
    { "me", "麼" },
    { "mei", "美妹每沒毎媒" },
    { "men", "們" },
    { "mi", "米密秘" },
    { "mian", "免面勉眠" },
    { "miao", "描" },
    { "min", "民皿" },
    { "ming", "命明名" },
    { "mo", "末模麼" },
    { "mou", "某" },
    { "mu", "母木目模" },
    { "na", "那哪拿內南" },
    { "nan", "男南難" },
    { "nao", "腦" },
    { "ne", "那哪呢" },
    { "nei", "内那哪內" },
    { "neng", "能" },
    { "ni", "你妳呢" },
    { "nian", "年念" },
    { "niang", "娘" },
    { "nin", "您" },
    { "ning", "凝" },
    { "niu", "牛" },
    { "nong", "農濃" },
    { "nu", "女努" },
    { "nuan", "暖" },
    { "o", "" },
    { "ou", "歐" },
    { "pa", "怕" },
    { "pian", "片便" },
    { "pai", "迫派排" },
    { "pan", "判番" },
    { "pang", "旁" },
    { "pei", "配" },
    { "peng", "朋" },
    { "pi", "疲否" },
    { "pin", "品貧" },
    { "ping", "平評" },
    { "po", "迫破泊頗" },
    { "pu", "普僕" },
    { "qi", "起其奇七气期泣企妻契気" },
    { "qian", "嵌浅千前鉛錢針" },
    { "qiang", "強將" },
    { "qiao", "橋繰" },
    { "qie", "且切契" },
    { "qin", "寝勤親" },
    { "qing", "青清情晴輕頃請軽" },
    { "qiu", "求秋球" },
    { "qu", "去取趣曲區" },
    { "quan", "全犬券" },
    { "que", "缺確卻" },
    { "ran", "然" },
    { "rang", "讓" },
    { "re", "熱" },
    { "ren", "人任認" },
    { "reng", "仍" },
    { "ri", "日" },
    { "rong", "容" },
    { "rou", "弱若肉" },
    { "ru", "如入" },
    { "ruan", "軟" },
    { "sai", "賽" },
    { "san", "三" },
    { "sao", "騒繰" },
    { "se", "色" },
    { "sen", "森" },
    { "sha", "砂" },
    { "shan", "善山單" },
    { "shang", "上尚商" },
    { "shao", "少紹" },
    { "shaung", "雙" },
    { "she", "社射設捨渉" },
    { "shei", "誰" },
    { "shen", "什申深甚身伸沈神" },
    { "sheng", "生声昇勝乗聲" },
    { "shi", "是失示食时事式十石施使世实史室市始柿氏士仕拭時視師試適実實識" },
    { "shou", "手首守受授" },
    { "shu", "束数暑殊樹書屬輸術" },
    { "shui", "水説說誰" },
    { "shuo", "数説說" },
    { "si", "思寺司四私似死価" },
    { "song", "送" },
    { "su", "速宿素蘇訴" },
    { "suan", "算酸" },
    { "sui", "隨雖歲歳" },
    { "sun", "孫" },
    { "suo", "所" },
    { "ta", "她他它牠" },
    { "tai", "太台態臺" },
    { "tan", "探談曇" },
    { "tang", "糖" },
    { "tao", "桃逃套討" },
    { "te", "特" },
    { "ti", "体提替題體戻" },
    { "tian", "天田" },
    { "tiao", "条條調" },
    { "tie", "鉄" },
    { "ting", "停庭聽町" },
    { "tong", "同童通痛统統" },
    { "tou", "投透頭" },
    { "tu", "土徒茶図" },
    { "tuan", "團" },
    { "tui", "推退" },
    { "tuo", "脱駄" },
    { "u", "" },
    { "v", "" },
    { "wai", "外" },
    { "wan", "完万玩晩腕灣" },
    { "wang", "忘望亡往網" },
    { "wei", "危位未味委為謂維違圍" },
    { "wen", "文温問聞" },
    { "wo", "我" },
    { "wu", "午物五無屋亡鳥務汚" },
    { "xi", "夕息西洗喜系昔席希析嬉膝細習係" },
    { "xia", "下夏狭暇" },
    { "xian", "先限嫌洗現見線顯" },
    { "xiang", "向相香像想象降項詳響" },
    { "xiao", "小笑消效校削咲" },
    { "xie", "写携些解邪械協謝寫契" },
    { "xin", "心信新辛" },
    { "xing", "行形性幸型星興" },
    { "xiong", "兄胸" },
    { "xiu", "休秀修" },
    { "xu", "須需許續緒続" },
    { "xuan", "選懸" },
    { "xue", "学雪削靴學" },
    { "xun", "訓訊" },
    { "ya", "呀押壓" },
    { "yan", "言顔研煙嚴厳験驗塩" },
    { "yang", "央洋陽樣様" },
    { "yao", "要揺腰薬曜" },
    { "ye", "也野夜邪業葉" },
    { "yi", "一已亦依以移意医易伊役異億義議藝醫訳" },
    { "yin", "因引音飲銀" },
    { "ying", "英迎影映應營営" },
    { "yong", "永用泳擁" },
    { "you", "又有右友由尤油遊郵誘優" },
    { "yu", "予育余雨浴欲愈御宇域語於魚與込" },
    { "yuan", "元原源院員円園遠猿願" },
    { "yue", "月越約楽" },
    { "yun", "雲伝運" },
    { "za", "雑" },
    { "zai", "在再載災" },
    { "zang", "蔵" },
    { "zao", "早造" },
    { "ze", "則擇責" },
    { "zen", "怎" },
    { "zeng", "曾增増" },
    { "zha", "札" },
    { "zhai", "宅擇" },
    { "zhan", "站展戰戦" },
    { "zhang", "丈長障帳張" },
    { "zhao", "找着朝招" },
    { "zhe", "者這" },
    { "zhen", "真震針" },
    { "zheng", "正整争政爭" },
    { "zhi", "之只知支止制至治直指值置智値紙製質誌織隻識職執" },
    { "zhong", "中种終重種眾" },
    { "zhou", "周州昼宙洲週" },
    { "zhu", "助主住柱株祝逐注著諸屬術" },
    { "zhuan", "专專転" },
    { "zhuang", "状狀" },
    { "zhui", "追" },
    { "zhun", "準" },
    { "zhuo", "着" },
    { "zi", "子自字姉資" },
    { "zong", "總" },
    { "zuo", "左做昨坐座作" },
    { "zu", "足祖族卒組" },
    { "zui", "最酔" },
    { "zou", "走" },
    {NULL, NULL}
};
#endif


/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * lv_ime_pinyin_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}


/*=====================
 * Setter functions
 *====================*/

/**
 * Set the keyboard of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @param dict pointer to a Pinyin input method keyboard
 */
void lv_ime_pinyin_set_keyboard(lv_obj_t * obj, lv_obj_t * kb)
{
    if(kb) {
        LV_ASSERT_OBJ(kb, &lv_keyboard_class);
    }

    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(pinyin_ime->kb && lv_obj_is_valid(pinyin_ime->kb)) {
        lv_obj_remove_event_cb(pinyin_ime->kb, lv_ime_pinyin_kb_event);
        if(pinyin_ime->language_button) lv_obj_del(pinyin_ime->language_button);
    }
    pinyin_ime->language_button = pinyin_ime->language_label = NULL;
    pinyin_ime->kb = kb;
    pinyin_ime->textarea = NULL;
    pinyin_ime_clear_data(obj);
    if(!kb) return;
    lv_obj_add_event_cb(kb, lv_ime_pinyin_kb_event, LV_EVENT_ALL, obj);
    lv_obj_align_to(pinyin_ime->cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    /* A real child button leaves the original keyboard maps untouched, including
     * keyboards elsewhere that accept only passwords or URLs. */
    pinyin_ime->language_button = lv_btn_create(kb);
    lv_obj_set_style_pad_all(pinyin_ime->language_button, 0, 0);
    lv_obj_set_style_radius(pinyin_ime->language_button, 8, 0);
    lv_obj_set_style_shadow_width(pinyin_ime->language_button, 0, 0);
    lv_obj_set_style_bg_color(pinyin_ime->language_button, lv_color_hex(0xD7EBDF), 0);
    lv_obj_set_style_text_color(pinyin_ime->language_button, lv_color_hex(0x285843), 0);
    lv_obj_clear_flag(pinyin_ime->language_button, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    pinyin_ime->language_label = lv_label_create(pinyin_ime->language_button);
    lv_obj_set_style_text_font(pinyin_ime->language_label, LV_FONT_DEFAULT, 0);
    lv_obj_center(pinyin_ime->language_label);
    lv_obj_add_event_cb(pinyin_ime->language_button, pinyin_language_event, LV_EVENT_CLICKED, obj);
    pinyin_sync_language(obj);
}

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @param dict pointer to a Pinyin input method dictionary
 */
void lv_ime_pinyin_set_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    init_pinyin_dict(obj, dict);
}

/**
 * Set mode, 26-key input(k26) or 9-key input(k9).
 * @param obj  pointer to a Pinyin input method object
 * @param mode   the mode from 'lv_keyboard_mode_t'
 */
void lv_ime_pinyin_set_mode(lv_obj_t * obj, lv_ime_pinyin_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    LV_ASSERT_OBJ(pinyin_ime->kb, &lv_keyboard_class);

    pinyin_ime->mode = mode;

#if LV_IME_PINYIN_USE_K9_MODE
    if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
        pinyin_k9_init_data(obj);
        lv_keyboard_set_map(pinyin_ime->kb, LV_KEYBOARD_MODE_USER_1, lv_btnm_def_pinyin_k9_map,
                            default_kb_ctrl_k9_map);
        lv_keyboard_set_mode(pinyin_ime->kb, LV_KEYBOARD_MODE_USER_1);
    }
#endif
}

void lv_ime_pinyin_set_enabled(lv_obj_t * obj, bool enabled)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    pinyin_ime_clear_data(obj);
    ime->enabled = enabled;
    pinyin_sync_language(obj);
}

bool lv_ime_pinyin_get_enabled(lv_obj_t * obj)
{
    return ((lv_ime_pinyin_t *)obj)->enabled;
}

void lv_ime_pinyin_clear(lv_obj_t * obj)
{
    pinyin_ime_clear_data(obj);
}

void lv_ime_pinyin_refresh(lv_obj_t * obj)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    pinyin_sync_language(obj);
    if(ime->enabled && ime->ta_count) pinyin_input_proc(obj);
    else lv_obj_add_flag(ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin IME object
 * @return     pointer to the Pinyin IME keyboard
 */
lv_obj_t * lv_ime_pinyin_get_kb(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->kb;
}

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @return     pointer to the Pinyin input method candidate panel
 */
lv_obj_t * lv_ime_pinyin_get_cand_panel(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->cand_panel;
}

/**
 * Set the dictionary of Pinyin input method.
 * @param obj  pointer to a Pinyin input method object
 * @return     pointer to the Pinyin input method dictionary
 */
lv_pinyin_dict_t * lv_ime_pinyin_get_dict(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->dict;
}

/*=====================
 * Other functions
 *====================*/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_ime_pinyin_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->enabled = true;
    pinyin_ime->candidate_map[0] = "";

    pinyin_ime->mode = LV_IME_PINYIN_MODE_K26;
    pinyin_ime->py_page = 0;
    pinyin_ime->ta_count = 0;
    pinyin_ime->cand_num = 0;
    lv_memset_00(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
    lv_memset_00(pinyin_ime->py_num, sizeof(pinyin_ime->py_num));
    lv_memset_00(pinyin_ime->py_pos, sizeof(pinyin_ime->py_pos));

    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(55));
    lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);

#if LV_IME_PINYIN_USE_DEFAULT_DICT
    init_pinyin_dict(obj, lv_ime_pinyin_def_dict);
#endif

    /* Init pinyin_ime->cand_panel */
    pinyin_ime->cand_panel = lv_btnmatrix_create(lv_scr_act());
    lv_btnmatrix_set_map(pinyin_ime->cand_panel, pinyin_ime->candidate_map);
    lv_obj_set_size(pinyin_ime->cand_panel, LV_PCT(100), LV_PCT(5));
    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);

    lv_btnmatrix_set_one_checked(pinyin_ime->cand_panel, true);

    /* Set cand_panel style*/
    // Default style
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_pad_all(pinyin_ime->cand_panel, 8, 0);
    lv_obj_set_style_pad_gap(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_radius(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_pad_gap(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_base_dir(pinyin_ime->cand_panel, LV_BASE_DIR_LTR, 0);

    // LV_PART_ITEMS style
    lv_obj_set_style_radius(pinyin_ime->cand_panel, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pinyin_ime->cand_panel, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_opa(pinyin_ime->cand_panel, LV_OPA_0, LV_PART_ITEMS);

    // LV_PART_ITEMS | LV_STATE_PRESSED style
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(pinyin_ime->cand_panel, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);

    pinyin_render_candidates(obj);

    /* event handler */
    lv_obj_add_event_cb(pinyin_ime->cand_panel, lv_ime_pinyin_cand_panel_event, LV_EVENT_VALUE_CHANGED, obj);
    lv_obj_add_event_cb(obj, lv_ime_pinyin_style_change_event, LV_EVENT_STYLE_CHANGED, NULL);

#if LV_IME_PINYIN_USE_K9_MODE
    pinyin_ime->k9_input_str_len = 0;
    pinyin_ime->k9_py_ll_pos = 0;
    pinyin_ime->k9_legal_py_count = 0;
    lv_memset_00(pinyin_ime->k9_input_str, LV_IME_PINYIN_K9_MAX_INPUT);

    pinyin_k9_init_data(obj);

    _lv_ll_init(&(pinyin_ime->k9_legal_py_ll), sizeof(ime_pinyin_k9_py_str_t));
#endif
}


static void lv_ime_pinyin_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(lv_obj_is_valid(pinyin_ime->kb)) {
        lv_obj_remove_event_cb(pinyin_ime->kb, lv_ime_pinyin_kb_event);
        lv_obj_del(pinyin_ime->kb);
    }

    if(lv_obj_is_valid(pinyin_ime->cand_panel))
        lv_obj_del(pinyin_ime->cand_panel);
}


/* Return only complete UTF-8 scalars. Invalid/truncated SD rows cannot become
 * unterminated candidate strings or make the reader pass the string end. */
static uint8_t pinyin_utf8_size(const char * text)
{
    const unsigned char * p = (const unsigned char *)text;
    if(!p || !*p) return 0;
    uint8_t n = p[0] < 0x80 ? 1 : p[0] >= 0xC2 && p[0] <= 0xDF ? 2 :
                p[0] >= 0xE0 && p[0] <= 0xEF ? 3 : p[0] >= 0xF0 && p[0] <= 0xF4 ? 4 : 0;
    for(uint8_t i = 1; i < n; ++i) if((p[i] & 0xC0) != 0x80) return 0;
    if(n == 3 && ((p[0] == 0xE0 && p[1] < 0xA0) || (p[0] == 0xED && p[1] >= 0xA0))) return 0;
    if(n == 4 && ((p[0] == 0xF0 && p[1] < 0x90) || (p[0] == 0xF4 && p[1] > 0x8F))) return 0;
    return n;
}

static uint16_t pinyin_count_candidates(const char * text)
{
    uint16_t count = 0;
    uint8_t size;
    while(count < UINT16_MAX && (size = pinyin_utf8_size(text)) != 0) { text += size; ++count; }
    return count;
}

static int pinyin_language_slot(lv_obj_t * kb)
{
    for(uint16_t i = 0; i < 64; i++) {
        const char * text = lv_btnmatrix_get_btn_text(kb, i);
        if(!text) break;
        if(!strcmp(text, "CN") || !strcmp(text, "EN")) return i;
    }
    return -1;
}

static void pinyin_sync_language(lv_obj_t * obj)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    if(!ime->kb || !ime->language_button) return;
    static bool placing;
    if(placing) return;
    placing = true;
    lv_obj_t * ta = lv_keyboard_get_textarea(ime->kb);
    if(ta != ime->textarea) {
        pinyin_ime_clear_data(obj);
        ime->textarea = ta;
    }
    lv_label_set_text(ime->language_label, ime->enabled ? "CN" : "EN");
    lv_obj_set_style_text_font(ime->language_label, &lv_font_montserrat_20, 0);
    lv_obj_update_layout(ime->kb);
    int slot = pinyin_language_slot(ime->kb);
    bool show = slot >= 0 && ta && !lv_textarea_get_password_mode(ta);
    if(show) {
        /* button_areas include the keyboard padding; child align starts inside it. */
        lv_btnmatrix_t * btnm = (lv_btnmatrix_t *)ime->kb;
        const lv_area_t * area = &btnm->button_areas[slot];
        lv_coord_t border = lv_obj_get_style_border_width(ime->kb, LV_PART_MAIN);
        lv_coord_t pad_l = lv_obj_get_style_pad_left(ime->kb, LV_PART_MAIN) + border;
        lv_coord_t pad_t = lv_obj_get_style_pad_top(ime->kb, LV_PART_MAIN) + border;
        lv_obj_set_size(ime->language_button, lv_area_get_width(area), lv_area_get_height(area));
        lv_obj_align(ime->language_button, LV_ALIGN_TOP_LEFT, area->x1 - pad_l, area->y1 - pad_t);
        lv_obj_move_foreground(ime->language_button);
        lv_obj_clear_flag(ime->language_button, LV_OBJ_FLAG_HIDDEN);
    }
    else lv_obj_add_flag(ime->language_button, LV_OBJ_FLAG_HIDDEN);
    placing = false;
}

static void pinyin_language_event(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_ime_pinyin_set_enabled(obj, !lv_ime_pinyin_get_enabled(obj));
}

/* The composition must still be immediately before the cursor. External edits,
 * rejected characters and cursor movement must never delete unrelated text. */
static bool pinyin_composition_matches(lv_ime_pinyin_t * ime, lv_obj_t * ta)
{
    uint32_t cursor = lv_textarea_get_cursor_pos(ta);
    size_t len = strlen(ime->input_char);
    if(!len || len != ime->ta_count || cursor < len) return false;
    const char * text = lv_textarea_get_text(ta);
    uint32_t offset = _lv_txt_encoded_get_byte_id(text, cursor - len);
    for(size_t i = 0; i < len; ++i) {
        char c = text[offset + i];
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c != ime->input_char[i]) return false;
    }
    return true;
}

static void lv_ime_pinyin_kb_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    if(code == LV_EVENT_DELETE) {
        ime->kb = ime->language_button = ime->language_label = ime->textarea = NULL;
        pinyin_ime_clear_data(obj);
        return;
    }
    if(code == LV_EVENT_SIZE_CHANGED || code == LV_EVENT_REFRESH) {
        pinyin_sync_language(obj);
        return;
    }
    if(code != LV_EVENT_VALUE_CHANGED || !ime->kb) return;
    pinyin_sync_language(obj);
    lv_obj_t * ta = lv_keyboard_get_textarea(ime->kb);
    if(!ta || !ime->enabled || lv_textarea_get_password_mode(ta)) {
        pinyin_ime_clear_data(obj);
        return;
    }
    uint16_t id = lv_btnmatrix_get_selected_btn(ime->kb);
    if(id == LV_BTNMATRIX_BTN_NONE) return;
    const char * txt = lv_btnmatrix_get_btn_text(ime->kb, id);
    if(!txt) return;
    uint32_t cursor = lv_textarea_get_cursor_pos(ta);
    if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if(ime->ta_count && cursor + 1 == ime->composition_end) {
            ime->input_char[--ime->ta_count] = '\0';
            ime->composition_end = cursor;
            if(ime->ta_count && pinyin_composition_matches(ime, ta)) pinyin_input_proc(obj);
            else pinyin_ime_clear_data(obj);
        }
        else pinyin_ime_clear_data(obj);
    }
    else if(txt[0] && !txt[1] && ((txt[0] >= 'a' && txt[0] <= 'z') || (txt[0] >= 'A' && txt[0] <= 'Z'))) {
        /* Keep existing Latin text on overflow; begin a bounded new syllable. */
        if(ime->ta_count && (ime->ta_count + 1 >= sizeof(ime->input_char) || cursor != ime->composition_end + 1))
            pinyin_ime_clear_data(obj);
        size_t len = strlen(ime->input_char);
        ime->input_char[len] = txt[0] >= 'A' && txt[0] <= 'Z' ? txt[0] + ('a' - 'A') : txt[0];
        ime->input_char[len + 1] = '\0';
        ime->ta_count = len + 1;
        ime->composition_end = cursor;
        if(pinyin_composition_matches(ime, ta)) pinyin_input_proc(obj);
        else pinyin_ime_clear_data(obj);
    }
    else pinyin_ime_clear_data(obj);
}

static void pinyin_set_expanded(lv_obj_t * obj, bool expanded)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    if(ime->expanded == expanded) return;
    lv_obj_t * panel = ime->cand_panel;
    if(expanded) {
        lv_obj_update_layout(panel);
        ime->normal_height = lv_obj_get_height(panel);
        ime->normal_x = lv_obj_get_style_x(panel, 0);
        ime->normal_y = lv_obj_get_style_y(panel, 0);
        ime->normal_align = lv_obj_get_style_align(panel, 0);
        ime->normal_opa = lv_obj_get_style_bg_opa(panel, 0);
        ime->normal_color = lv_obj_get_style_bg_color(panel, 0);
        lv_area_t bounds;
        lv_obj_get_coords(panel, &bounds);
        lv_coord_t height = lv_disp_get_ver_res(lv_obj_get_disp(panel)) - bounds.y1;
        lv_obj_set_height(panel, LV_MAX(height, ime->normal_height));
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xF1F8F3), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_move_foreground(panel);
    }
    else {
        lv_obj_set_height(panel, ime->normal_height);
        lv_obj_set_align(panel, ime->normal_align);
        lv_obj_set_pos(panel, ime->normal_x, ime->normal_y);
        lv_obj_set_style_bg_opa(panel, ime->normal_opa, 0);
        lv_obj_set_style_bg_color(panel, ime->normal_color, 0);
    }
    ime->expanded = expanded;
    ime->py_page = 0;
}

static void pinyin_render_candidates(lv_obj_t * obj)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    unsigned capacity = ime->expanded ? LV_IME_PINYIN_EXPANDED_COUNT : LV_IME_PINYIN_CAND_TEXT_NUM;
    unsigned start = ime->py_page * capacity;
    const char * text = ime->cand_str;
    for(unsigned i = 0; text && i < start && i < ime->cand_num; ++i) {
        uint8_t size = pinyin_utf8_size(text);
        if(!size) { text = NULL; break; }
        text += size;
    }
    unsigned valid = 0;
    for(unsigned i = 0; i < capacity; ++i) {
        strcpy(ime->candidate_text[i], " ");
        uint8_t size = text && start + i < ime->cand_num ? pinyin_utf8_size(text) : 0;
        if(size) {
            memcpy(ime->candidate_text[i], text, size);
            ime->candidate_text[i][size] = '\0';
            text += size;
            ++valid;
        }
    }
    unsigned map = 0;
    if(ime->expanded) {
        unsigned pages = ime->cand_num ? (ime->cand_num + capacity - 1) / capacity : 1;
        lv_snprintf(ime->page_text, sizeof(ime->page_text), "%u/%u", ime->py_page + 1, pages);
        ime->candidate_map[map++] = "上一页";
        ime->candidate_map[map++] = ime->page_text;
        ime->candidate_map[map++] = "下一页";
        ime->candidate_map[map++] = "收起";
        ime->candidate_map[map++] = "\n";
        for(unsigned i = 0; i < capacity; ++i) {
            ime->candidate_map[map++] = ime->candidate_text[i];
            if((i + 1) % LV_IME_PINYIN_CAND_TEXT_NUM == 0 && i + 1 < capacity) ime->candidate_map[map++] = "\n";
        }
    }
    else {
        ime->candidate_map[map++] = "<";
        for(unsigned i = 0; i < capacity; ++i) ime->candidate_map[map++] = ime->candidate_text[i];
        ime->candidate_map[map++] = ">";
        ime->candidate_map[map++] = "更多";
    }
    ime->candidate_map[map] = "";
    lv_btnmatrix_set_map(ime->cand_panel, ime->candidate_map);
    /* Expanded ids can exceed the normal map after a selection/collapse. */
    lv_btnmatrix_set_selected_btn(ime->cand_panel, LV_BTNMATRIX_BTN_NONE);
    lv_btnmatrix_clear_btn_ctrl_all(ime->cand_panel, LV_BTNMATRIX_CTRL_DISABLED | LV_BTNMATRIX_CTRL_CHECKED);
    /* Resize only on release. Otherwise the held finger can hit another row
     * immediately after More/Collapse changes the button geometry. */
    lv_btnmatrix_set_btn_ctrl_all(ime->cand_panel, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    unsigned first = ime->expanded ? 4 : 1;
    for(unsigned i = valid; i < capacity; ++i) lv_btnmatrix_set_btn_ctrl(ime->cand_panel, first + i, LV_BTNMATRIX_CTRL_DISABLED);
    if(ime->py_page == 0) lv_btnmatrix_set_btn_ctrl(ime->cand_panel, 0, LV_BTNMATRIX_CTRL_DISABLED);
    if(start + capacity >= ime->cand_num) lv_btnmatrix_set_btn_ctrl(ime->cand_panel, ime->expanded ? 2 : capacity + 1, LV_BTNMATRIX_CTRL_DISABLED);
    if(ime->expanded) lv_btnmatrix_set_btn_ctrl(ime->cand_panel, 1, LV_BTNMATRIX_CTRL_DISABLED);
    lv_obj_invalidate(ime->cand_panel);
}

static void lv_ime_pinyin_cand_panel_event(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    uint32_t id = lv_btnmatrix_get_selected_btn(ime->cand_panel);
    if(id == LV_BTNMATRIX_BTN_NONE || !ime->enabled || !ime->kb) return;
    lv_obj_t * ta = lv_keyboard_get_textarea(ime->kb);
    if(!ta || ta != ime->textarea || !ime->cand_str || !ime->cand_num || !pinyin_composition_matches(ime, ta)) {
        pinyin_ime_clear_data(obj);
        return;
    }
    if(ime->expanded) {
        if(id == 0 || id == 2) { pinyin_page_proc(obj, id == 2); return; }
        if(id == 3) { pinyin_set_expanded(obj, false); pinyin_render_candidates(obj); return; }
        if(id < 4 || id >= 4 + LV_IME_PINYIN_EXPANDED_COUNT) return;
    }
    else {
        if(id == 0 || id == LV_IME_PINYIN_CAND_TEXT_NUM + 1) { pinyin_page_proc(obj, id != 0); return; }
        if(id == LV_IME_PINYIN_CAND_TEXT_NUM + 2) { pinyin_set_expanded(obj, true); pinyin_render_candidates(obj); return; }
        if(id < 1 || id > LV_IME_PINYIN_CAND_TEXT_NUM) return;
    }
    unsigned capacity = ime->expanded ? LV_IME_PINYIN_EXPANDED_COUNT : LV_IME_PINYIN_CAND_TEXT_NUM;
    unsigned index = id - (ime->expanded ? 4 : 1);
    if(ime->py_page * capacity + index >= ime->cand_num || !ime->matched_len) return;
    /* Copy before reset: map and text storage belong to this IME instance. */
    char chosen[5]; strcpy(chosen, ime->candidate_text[index]);
    char remaining[sizeof(ime->input_char)];
    strcpy(remaining, ime->input_char + ime->matched_len);
    unsigned count = ime->ta_count;
    pinyin_ime_clear_data(obj);
    for(unsigned i = 0; i < count; ++i) lv_textarea_del_char(ta);
    lv_textarea_add_text(ta, chosen);
    lv_textarea_add_text(ta, remaining);
    strcpy(ime->input_char, remaining);
    ime->ta_count = strlen(remaining);
    ime->composition_end = lv_textarea_get_cursor_pos(ta);
    if(ime->ta_count && pinyin_composition_matches(ime, ta)) pinyin_input_proc(obj);
    else pinyin_ime_clear_data(obj);
}

static void pinyin_input_proc(lv_obj_t * obj)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    pinyin_set_expanded(obj, false);
    ime->py_page = 0;
    ime->cand_num = 0;
    ime->matched_len = 0;
    ime->cand_str = pinyin_search_matching(obj, ime->input_char, &ime->cand_num);
    pinyin_render_candidates(obj);
    if(ime->cand_str && ime->cand_num) lv_obj_clear_flag(ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
}

static void pinyin_page_proc(lv_obj_t * obj, uint16_t dir)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    if(!ime->cand_str || !ime->cand_num) return;
    unsigned capacity = ime->expanded ? LV_IME_PINYIN_EXPANDED_COUNT : LV_IME_PINYIN_CAND_TEXT_NUM;
    if(dir == 0) { if(ime->py_page) --ime->py_page; }
    else if((ime->py_page + 1) * capacity < ime->cand_num) ++ime->py_page;
    pinyin_render_candidates(obj);
}

static void lv_ime_pinyin_style_change_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(code == LV_EVENT_STYLE_CHANGED) {
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_obj_set_style_text_font(pinyin_ime->cand_panel, font, 0);
    }
}


static void init_pinyin_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    /*
     * The original LVGL implementation assumes that the dictionary has at
     * least one row for every letter from 'a' through the current row.  That
     * is not true for normal pinyin dictionaries: i, u and v are initials
     * only in the keyboard spelling scheme and do not have dictionary rows.
     * Walking the actual rows keeps the position/count pair correct when a
     * letter is absent, and also makes SD supplied dictionaries safe.
     */
    pinyin_ime->dict = dict;

    lv_memset_00(pinyin_ime->py_num, sizeof(pinyin_ime->py_num));
    lv_memset_00(pinyin_ime->py_pos, sizeof(pinyin_ime->py_pos));

    pinyin_ime_clear_data(obj);
    pinyin_ime->dict_count = 0;
    if(!dict) return;
    for(uint16_t i = 0; i < UINT16_MAX && dict[i].py != NULL && dict[i].py_mb != NULL; i++) {
        pinyin_ime->dict_count++;
        const char headletter = dict[i].py[0];
        if(headletter < 'a' || headletter > 'z') continue;

        const uint8_t offset = (uint8_t)(headletter - 'a');
        if(pinyin_ime->py_num[offset] == 0) {
            pinyin_ime->py_pos[offset] = i;
        }
        pinyin_ime->py_num[offset]++;
    }
}


static char * pinyin_search_matching(lv_obj_t * obj, char * py_str, uint16_t * cand_num)
{
    lv_ime_pinyin_t * ime = (lv_ime_pinyin_t *)obj;
    *cand_num = 0;
    size_t len = strlen(py_str), best_len = 0;
    lv_pinyin_dict_t * best = NULL, * completion = NULL;
    if(!len || !ime->dict) return NULL;
    /* Scan actual rows: fallback dictionaries need not be alphabetically grouped.
     * Prefer exact/longest syllable, then an unfinished-syllable completion. */
    for(unsigned i = 0; i < ime->dict_count; ++i) {
        lv_pinyin_dict_t * row = &ime->dict[i];
        size_t n = strlen(row->py);
        if(!n || !row->py_mb[0]) continue;
        if(n <= len && n > best_len && !strncmp(py_str, row->py, n)) { best = row; best_len = n; }
        if(!completion && n > len && !strncmp(py_str, row->py, len)) completion = row;
    }
    if(!best) { best = completion; best_len = len; }
    if(!best) return NULL;
    *cand_num = pinyin_count_candidates(best->py_mb);
    ime->matched_len = best_len;
    return *cand_num ? (char *)best->py_mb : NULL;
}

static void pinyin_ime_clear_data(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;


#if LV_IME_PINYIN_USE_K9_MODE
    if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
        pinyin_ime->k9_input_str_len = 0;
        pinyin_ime->k9_py_ll_pos = 0;
        pinyin_ime->k9_legal_py_count = 0;
        lv_memset_00(pinyin_ime->k9_input_str,  LV_IME_PINYIN_K9_MAX_INPUT);
        lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
    }
#endif

    pinyin_ime->ta_count = 0;
    pinyin_ime->cand_str = NULL;
    pinyin_ime->cand_num = pinyin_ime->py_page = pinyin_ime->matched_len = 0;
    pinyin_ime->composition_end = 0;
    lv_memset_00(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
    if(pinyin_ime->cand_panel) {
        pinyin_set_expanded(obj, false);
        pinyin_render_candidates(obj);
        lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
}


#if LV_IME_PINYIN_USE_K9_MODE
static void pinyin_k9_init_data(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t py_str_i = 0;
    uint16_t btnm_i = 0;
    for(btnm_i = 19; btnm_i < (LV_IME_PINYIN_K9_CAND_TEXT_NUM + 21); btnm_i++) {
        if(py_str_i == LV_IME_PINYIN_K9_CAND_TEXT_NUM) {
            strcpy(lv_pinyin_k9_cand_str[py_str_i], LV_SYMBOL_RIGHT"\0");
        }
        else if(py_str_i == LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1) {
            strcpy(lv_pinyin_k9_cand_str[py_str_i], "\0");
        }
        else {
            strcpy(lv_pinyin_k9_cand_str[py_str_i], " \0");
        }

        lv_btnm_def_pinyin_k9_map[btnm_i] = lv_pinyin_k9_cand_str[py_str_i];
        py_str_i++;
    }

    default_kb_ctrl_k9_map[0]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[4]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[5]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[9]  = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[10] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[14] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[15] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
    default_kb_ctrl_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 16] = LV_KEYBOARD_CTRL_BTN_FLAGS | 1;
}

static void pinyin_k9_get_legal_py(lv_obj_t * obj, char * k9_input, const char * py9_map[])
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t len = strlen(k9_input);

    if((len == 0) || (len >= LV_IME_PINYIN_K9_MAX_INPUT)) {
        return;
    }

    char py_comp[LV_IME_PINYIN_K9_MAX_INPUT] = {0};
    int mark[LV_IME_PINYIN_K9_MAX_INPUT] = {0};
    int index = 0;
    int flag = 0;
    int count = 0;

    uint32_t ll_len = 0;
    ime_pinyin_k9_py_str_t * ll_index = NULL;

    ll_len = _lv_ll_get_len(&pinyin_ime->k9_legal_py_ll);
    ll_index = _lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);

    while(index != -1) {
        if(index == len) {
            if(pinyin_k9_is_valid_py(obj, py_comp)) {
                if((count >= ll_len) || (ll_len == 0)) {
                    ll_index = _lv_ll_ins_tail(&pinyin_ime->k9_legal_py_ll);
                    strcpy(ll_index->py_str, py_comp);
                }
                else if((count < ll_len)) {
                    strcpy(ll_index->py_str, py_comp);
                    ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index);
                }
                count++;
            }
            index--;
        }
        else {
            flag = mark[index];
            if(flag < strlen(py9_map[k9_input[index] - '2'])) {
                py_comp[index] = py9_map[k9_input[index] - '2'][flag];
                mark[index] = mark[index] + 1;
                index++;
            }
            else {
                mark[index] = 0;
                index--;
            }
        }
    }

    if(count > 0) {
        pinyin_ime->ta_count++;
        pinyin_ime->k9_legal_py_count = count;
    }
}


/*true: visible; false: not visible*/
static bool pinyin_k9_is_valid_py(lv_obj_t * obj, char * py_str)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_pinyin_dict_t * cpHZ = NULL;
    uint8_t index = 0, len = 0, offset = 0;
    uint16_t ret = 1;
    volatile uint8_t count = 0;

    if(*py_str == '\0')    return false;
    if(*py_str == 'i')     return false;
    if(*py_str == 'u')     return false;
    if(*py_str == 'v')     return false;

    offset = py_str[0] - 'a';
    len = strlen(py_str);

    cpHZ  = &pinyin_ime->dict[pinyin_ime->py_pos[offset]];
    count = pinyin_ime->py_num[offset];

    while(count--) {
        for(index = 0; index < len; index++) {
            if(*(py_str + index) != *((cpHZ->py) + index)) {
                break;
            }
        }

        // perfect match
        if(len == 1 || index == len) {
            return true;
        }
        cpHZ++;
    }
    return false;
}


static void pinyin_k9_fill_cand(lv_obj_t * obj)
{
    static uint16_t len = 0;
    uint16_t index = 0, tmp_len = 0;
    ime_pinyin_k9_py_str_t * ll_index = NULL;

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    tmp_len = pinyin_ime->k9_legal_py_count;

    if(tmp_len != len) {
        lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
        len = tmp_len;
    }

    ll_index = _lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);
    strcpy(pinyin_ime->input_char, ll_index->py_str);
    while(ll_index) {
        if((index >= LV_IME_PINYIN_K9_CAND_TEXT_NUM) || \
           (index >= pinyin_ime->k9_legal_py_count))
            break;

        strcpy(lv_pinyin_k9_cand_str[index], ll_index->py_str);
        ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
        index++;
    }
    pinyin_ime->k9_py_ll_pos = index;

    lv_obj_t * ta = lv_keyboard_get_textarea(pinyin_ime->kb);
    for(index = 0; index < pinyin_ime->k9_input_str_len; index++) {
        lv_textarea_del_char(ta);
    }
    pinyin_ime->k9_input_str_len = strlen(pinyin_ime->input_char);
    lv_textarea_add_text(ta, pinyin_ime->input_char);
}


static void pinyin_k9_cand_page_proc(lv_obj_t * obj, uint16_t dir)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_obj_t * ta = lv_keyboard_get_textarea(pinyin_ime->kb);
    uint16_t ll_len =  _lv_ll_get_len(&pinyin_ime->k9_legal_py_ll);

    if((ll_len > LV_IME_PINYIN_K9_CAND_TEXT_NUM) && (pinyin_ime->k9_legal_py_count > LV_IME_PINYIN_K9_CAND_TEXT_NUM)) {
        ime_pinyin_k9_py_str_t * ll_index = NULL;
        uint16_t tmp_btn_str_len = 0;
        int count = 0;

        ll_index = _lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);
        while(ll_index) {
            if(count >= pinyin_ime->k9_py_ll_pos)   break;

            ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
            count++;
        }

        if((NULL == ll_index) && (dir == 1))   return;

        lv_memset_00(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");

        // next page
        if(dir == 1) {
            count = 0;
            while(ll_index) {
                if(count >= (LV_IME_PINYIN_K9_CAND_TEXT_NUM - 1))
                    break;

                strcpy(lv_pinyin_k9_cand_str[count], ll_index->py_str);
                ll_index = _lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
                count++;
            }
            pinyin_ime->k9_py_ll_pos += count - 1;

        }
        // previous page
        else {
            count = LV_IME_PINYIN_K9_CAND_TEXT_NUM - 1;
            ll_index = _lv_ll_get_prev(&pinyin_ime->k9_legal_py_ll, ll_index);
            while(ll_index) {
                if(count < 0)  break;

                strcpy(lv_pinyin_k9_cand_str[count], ll_index->py_str);
                ll_index = _lv_ll_get_prev(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the previous list*/
                count--;
            }

            if(pinyin_ime->k9_py_ll_pos > LV_IME_PINYIN_K9_CAND_TEXT_NUM)
                pinyin_ime->k9_py_ll_pos -= 1;
        }

        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

#endif  /*LV_IME_PINYIN_USE_K9_MODE*/

#endif  /*LV_USE_IME_PINYIN*/

