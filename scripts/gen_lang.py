#!/usr/bin/env python3
"""Generate lang/<code>.xml for fr/de/es/pt/it/ja from translation tables.

Usage:
    python3 scripts/gen_lang.py

Reads lang/en.xml for the canonical (zh) key list -- preserving order and acting as
a coverage check -- then emits one XML per language. Each file contains only the
keys we explicitly translate; anything missing falls back to the built-in English
TR[] at runtime (see src/i18n/i18n.c), which in turn falls back to Chinese.

The translation tables below are the single source of truth for these 6 languages.
Hand-editing the generated lang/*.xml will be overwritten on the next run.

Design notes:
  - Place names (countries / provinces) and the developer-facing "[Contours]" log
    lines are intentionally NOT translated here; they fall back to English, which
    is an acceptable, consistent behaviour for those proper-noun / log strings.
  - Multi-line zh keys (dialog detail paragraphs) are skipped for the same reason.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "lang", "en.xml")
OUT_DIR = os.path.join(ROOT, "lang")

# translations[zh] = {code: text}
T = {}


def add(zh, fr=None, de=None, es=None, pt=None, it=None, ja=None):
    vals = {}
    for code, v in (("fr", fr), ("de", de), ("es", es),
                    ("pt", pt), ("it", it), ("ja", ja)):
        if v is not None:
            vals[code] = v
    T[zh] = vals


# ---------------- 品牌 / 窗口标题 ----------------
add("跑路了", fr="Skedaddle", de="Skedaddle", es="Skedaddle",
    pt="Skedaddle", it="Skedaddle", ja="Skedaddle")

# ---------------- 设置面板标签（Qt 菜单 + C 核心面板共用）----------------
add("语言", fr="Langue", de="Sprache", es="Idioma", pt="Idioma", it="Lingua", ja="言語")
add("昼夜模式", fr="Mode jour/nuit", de="Tag/Nacht", es="Modo día/noche",
    pt="Modo dia/noite", it="Modalità giorno/notte", ja="昼夜モード")
add("导航面板", fr="Panneau de navigation", de="Navigationsbereich",
    es="Panel de navegación", pt="Painel de navegação",
    it="Pannello di navigazione", ja="ナビパネル")
add("POI 图标", fr="Icônes POI", de="POI-Symbole", es="Iconos POI",
    pt="Ícones POI", it="Icone POI", ja="POI アイコン")
add("地名标注", fr="Étiquettes de lieux", de="Ortsnamen", es="Etiquetas de lugares",
    pt="Rótulos de lugares", it="Etichette dei luoghi", ja="地名ラベル")
add("等高线", fr="Courbes de niveau", de="Höhenlinien", es="Curvas de nivel",
    pt="Curvas de nível", it="Curve di livello", ja="等高線")
add("网格细分刻度", fr="Sous-divisions de grille", de="Grid-Unterteilung",
    es="Subdivisiones de cuadrícula", pt="Subdivisões de grade",
    it="Suddivisioni griglia", ja="グリッド補助目盛")
add("语音播报", fr="Guidance vocale", de="Sprachansagen", es="Guía por voz",
    pt="Orientação por voz", it="Guida vocale", ja="音声案内")
add("默认页签", fr="Onglet par défaut", de="Standardregister",
    es="Pestaña predeterminada", pt="Aba padrão", it="Scheda predefinita", ja="既定タブ")
add("字号(pt)", fr="Taille de police (pt)", de="Schriftgröße (pt)",
    es="Tamaño de letra (pt)", pt="Tamanho da fonte (pt)",
    it="Dimensione carattere (pt)", ja="フォントサイズ(pt)")
add("道路细节", fr="Détail des routes", de="Straßendetails", es="Detalle de carreteras",
    pt="Detalhe das estradas", it="Dettaglio strade", ja="道路詳細")
add("坐标系", fr="Système géodésique", de="Geodätisches Datum", es="Datum",
    pt="Datum", it="Datum", ja="測地系")
add("网格类型", fr="Type de grille", de="Gittertyp", es="Tipo de cuadrícula",
    pt="Tipo de grade", it="Tipo di griglia", ja="グリッド種別")

# ---------------- 设置枚举选项 ----------------
add("白天", fr="Jour", de="Tag", es="Día", pt="Dia", it="Giorno", ja="昼")
add("夜间", fr="Nuit", de="Nacht", es="Noche", pt="Noite", it="Notte", ja="夜")
add("自动(日出日落)", fr="Auto (soleil)", de="Auto (Sonne)", es="Auto (sol)",
    pt="Automático (sol)", it="Auto (sole)", ja="自動(日の出/日没)")
add("浏览", fr="Parcourir", de="Durchsuchen", es="Explorar", pt="Explorar",
    it="Esplora", ja="ブラウズ")
add("导航", fr="Naviguer", de="Navigieren", es="Navegar", pt="Navegar",
    it="Naviga", ja="ナビ")
add("经纬网", fr="Grille lat/lon", de="Lat/Lon-Raster", es="Cuadrícula lat/lon",
    pt="Grade lat/lon", it="Griglia lat/lon", ja="緯度経度グリッド")
add("方里格", fr="Gauss-Krüger", de="Gauss-Krüger", es="Gauss-Krüger",
    pt="Gauss-Krüger", it="Gauss-Krüger", ja="ガウス・クリューガー")
add("WGS84（默认）", fr="WGS84 (par défaut)", de="WGS84 (Standard)",
    es="WGS84 (predeterminado)", pt="WGS84 (padrão)", it="WGS84 (predefinito)",
    ja="WGS84（既定）")
add("CGCS2000（2000国家大地坐标系）",
    fr="CGCS2000 (datum national 2000)", de="CGCS2000 (Staatsdatum 2000)",
    es="CGCS2000 (datum nacional 2000)", pt="CGCS2000 (datum nacional 2000)",
    it="CGCS2000 (datum nazionale 2000)", ja="CGCS2000（2000国家大地座標系）")
add("经纬网（度）", fr="Grille lat/lon (degrés)", de="Lat/Lon-Raster (Grad)",
    es="Cuadrícula lat/lon (grados)", pt="Grade lat/lon (graus)",
    it="Griglia lat/lon (gradi)", ja="緯度経度グリッド（度）")
add("方里格（公里）", fr="Gauss-Krüger (km)", de="Gauss-Krüger (km)",
    es="Gauss-Krüger (km)", pt="Gauss-Krüger (km)", it="Gauss-Krüger (km)",
    ja="ガウス・クリューガー（km）")
add("夜间模式", fr="Mode nuit", de="Nachtmodus", es="Modo noche",
    pt="Modo noite", it="Modalità notte", ja="夜間モード")
add("自动（日出日落）", fr="Auto (lever/coucher)", de="Auto (Sonnenaufgang/-untergang)",
    es="Auto (amanecer/atardecer)", pt="Automático (nascer/pôr do sol)",
    it="Auto (alba/tramonto)", ja="自動（日の出/日没）")

# ---------------- C 核心设置面板按钮 ----------------
add("设置", fr="Paramètres", de="Einstellungen", es="Ajustes",
    pt="Configurações", it="Impostazioni", ja="設定")
add("关", fr="Désactivé", de="Aus", es="Desactivado", pt="Desativado",
    it="Disattivato", ja="オフ")
add("开", fr="Activé", de="Ein", es="Activado", pt="Ativado", it="Attivato", ja="オン")
add("浮层透明度…", fr="Opacité des superpositions…", de="Deckkraft der Overlays…",
    es="Opacidad de superposiciones…", pt="Opacidade das sobreposições…",
    it="Opacità degli overlay…", ja="オーバーレイ透明度…")
add("逐个设置", fr="Par couche", de="Pro Ebene", es="Por capa", pt="Por camada",
    it="Per livello", ja="レイヤーごと")
add("重置全部", fr="Tout réinitialiser", de="Alle zurücksetzen", es="Restablecer todo",
    pt="Redefinir tudo", it="Ripristina tutto", ja="すべてリセット")
add("关闭", fr="Fermer", de="Schließen", es="Cerrar", pt="Fechar", it="Chiudi", ja="閉じる")

# ---------------- 主菜单 / 子菜单 ----------------
add("菜单(&M)", fr="Menu(&M)", de="Menü(&M)", es="Menú(&M)", pt="Menu(&M)",
    it="Menu(&M)", ja="メニュー(&M)")
add("搜索地点", fr="Rechercher un lieu", de="Ort suchen", es="Buscar lugar",
    pt="Buscar local", it="Cerca luogo", ja="場所を検索")
add("清除路线", fr="Effacer l'itinéraire", de="Route löschen", es="Borrar ruta",
    pt="Limpar rota", it="Cancella percorso", ja="ルートを消去")
add("缩放到路线", fr="Ajuster à l'itinéraire", de="Auf Route zoomen",
    es="Zoom a la ruta", pt="Zoom à rota", it="Zoom al percorso", ja="ルートに合わせてズーム")
add("跟随车头", fr="Suivre le cap", de="Kurs folgen", es="Seguir rumbo",
    pt="Seguir direção", it="Segui rotta", ja="進行方向に追随")
add("下载地图…", fr="Télécharger la carte…", de="Karte herunterladen…",
    es="Descargar mapa…", pt="Baixar mapa…", it="Scarica mappa…", ja="地図をダウンロード…")
add("退出", fr="Quitter", de="Beenden", es="Salir", pt="Sair", it="Esci", ja="終了")
add("模拟导航(&S)", fr="Simuler(&S)", de="Simulieren(&S)", es="Simular(&S)",
    pt="Simular(&S)", it="Simula(&S)", ja="シミュレート(&S)")
add("开始 / 停止", fr="Démarrer / Arrêter", de="Start / Stopp",
    es="Iniciar / Detener", pt="Iniciar / Parar", it="Avvia / Interrompi", ja="開始 / 停止")
add("注入偏航（触发重算）", fr="Injecter une déviation", de="Abweichung einfügen",
    es="Inyectar desviación", pt="Injetar desvio", it="Inserisci deviazione",
    ja="逸脱を注入（再計算）")
add("加速", fr="Accélérer", de="Beschleunigen", es="Acelerar", pt="Acelerar",
    it="Accelera", ja="加速")
add("减速", fr="Ralentir", de="Verlangsamen", es="Decelerar", pt="Desacelerar",
    it="Rallenta", ja="減速")
add("视图(&V)", fr="Affichage(&V)", de="Ansicht(&V)", es="Vista(&V)",
    pt="Vista(&V)", it="Vista(&V)", ja="表示(&V)")
add("道路细节 LOD %d", fr="Détail des routes LOD %d", de="Straßendetails LOD %d",
    es="Detalle de carreteras LOD %d", pt="Detalhe das estradas LOD %d",
    it="Dettaglio strade LOD %d", ja="道路詳細 LOD %d")
add("字号增大", fr="Police +", de="Schrift vergrößern", es="Letra mayor",
    pt="Fonte maior", it="Carattere +", ja="フォント拡大")
add("字号减小", fr="Police -", de="Schrift verkleinern", es="Letra menor",
    pt="Fonte menor", it="Carattere -", ja="フォント縮小")
add("坐标系(&C)", fr="Datum(&C)", de="Datum(&C)", es="Datum(&C)", pt="Datum(&C)",
    it="Datum(&C)", ja="測地系(&C)")
add("轨迹(&K)", fr="Traces(&K)", de="Spuren(&K)", es="Rutas(&K)",
    pt="Trilhas(&K)", it="Tracciati(&K)", ja="軌跡(&K)")
add("导入 GPX…", fr="Importer GPX…", de="GPX importieren…", es="Importar GPX…",
    pt="Importar GPX…", it="Importa GPX…", ja="GPX をインポート…")
add("导出全部为 GPX…", fr="Exporter tout en GPX…", de="Alles als GPX exportieren…",
    es="Exportar todo a GPX…", pt="Exportar tudo para GPX…",
    it="Esporta tutto in GPX…", ja="すべてを GPX にエクスポート…")
add("收藏视口中心", fr="Favoris au centre", de="Ansichtszentrum merken",
    es="Centrar favorito", pt="Favoritar centro", it="Preferiti al centro",
    ja="ビューポート中心をお気に入り")
add("轨迹 / 收藏面板", fr="Panneau Traces / Favoris", de="Panel Spuren / Favoriten",
    es="Panel Rutas / Favoritos", pt="Painel Trilhas / Favoritos",
    it="Pannello Tracciati / Preferiti", ja="軌跡 / お気に入りパネル")
add("设置(&P)", fr="Paramètres(&P)", de="Einstellungen(&P)", es="Ajustes(&P)",
    pt="Configurações(&P)", it="Impostazioni(&P)", ja="設定(&P)")
add("主菜单", fr="Menu principal", de="Hauptmenü", es="Menú principal",
    pt="Menu principal", it="Menu principale", ja="メインメニュー")
add("卫星天顶图（信号强弱 / 定位状态）",
    fr="Sky plot satellite (signal / statut)", de="Satelliten-Sky-Plot (Signal / Status)",
    es="Sky plot de satélites (señal / estado)", pt="Sky plot de satélites (sinal / estado)",
    it="Sky plot satellitare (segnale / stato)", ja="衛星スカイプロット（信号強度 / 測位状態）")
add("搜索地点或道路（Ctrl+F）", fr="Lieu ou route à rechercher (Ctrl+F)",
    de="Ort oder Straße suchen (Ctrl+F)", es="Buscar lugar o ruta (Ctrl+F)",
    pt="Buscar local ou rota (Ctrl+F)", it="Cerca luogo o strada (Ctrl+F)",
    ja="場所または道路を検索（Ctrl+F）")
add("搜索", fr="Rechercher", de="Suchen", es="Buscar", pt="Buscar", it="Cerca", ja="検索")
add("搜索地点、道路…", fr="Lieu, route à rechercher…", de="Ort, Straße…",
    es="Lugar, ruta…", pt="Local, rota…", it="Luogo, strada…", ja="場所、道路…")
add("左键循环切换 · 右键菜单", fr="Clic gauche : cycle · clic droit : menu",
    de="Links: zyklisch · Rechts: Menü", es="Clic izq.: ciclar · clic der.: menú",
    pt="Clique esq.: ciclar · clique dir.: menu", it="Clic sx: cicla · clic dx: menu",
    ja="左クリックで切替 · 右クリックでメニュー")
add("浮层透明度", fr="Opacité des superpositions", de="Deckkraft der Overlays",
    es="Opacidad de superposiciones", pt="Opacidade das sobreposições",
    it="Opacità degli overlay", ja="オーバーレイ透明度")
add("（C 核心）", fr="(cœur C)", de="(C-Kern)", es="(núcleo C)", pt="(núcleo C)",
    it="(core C)", ja="(C コア)")
add("默认", fr="Défaut", de="Standard", es="Predeterminado", pt="Padrão",
    it="Predefinito", ja="既定")
add("全部恢复默认（75%）", fr="Tout réinitialiser (75 %)",
    de="Alle auf Standard (75 %)", es="Restablecer todo (75 %)",
    pt="Redefinir tudo (75 %)", it="Ripristina tutto (75 %)", ja="すべて既定に戻す（75%）")
add("转向指令", fr="Instructions", de="Anweisungen", es="Indicaciones",
    pt="Indicações", it="Indicazioni", ja="案内")
add("轨迹 / 收藏", fr="Traces / Favoris", de="Spuren / Favoriten",
    es="Rutas / Favoritos", pt="Trilhas / Favoritos", it="Tracciati / Preferiti",
    ja="軌跡 / お気に入り")
add("导出全部…", fr="Exporter tout…", de="Alles exportieren…", es="Exportar todo…",
    pt="Exportar tudo…", it="Esporta tutto…", ja="すべてエクスポート…")
add("收藏中心点", fr="Favoris au centre", de="Ansichtszentrum", es="Centrar favorito",
    pt="Favoritar centro", it="Preferiti al centro", ja="お気に入り中心")
add("贴合", fr="Aimanter", de="Einrasten", es="Ajustar", pt="Ajustar", it="Aggancia",
    ja="スナップ")
add("把这条轨迹吸附到道路网（贴合路线）",
    fr="Aimanter cette trace au réseau routier",
    de="Diese Spur ans Straßennetz einrasten",
    es="Ajustar esta ruta a la red vial",
    pt="Ajustar esta trilha à rede viária",
    it="Aggancia questo tracciato alla rete stradale",
    ja="この軌跡を道路網にスナップ")
add("删除这一条", fr="Supprimer cet élément", de="Diesen Eintrag löschen",
    es="Eliminar este elemento", pt="Excluir este item", it="Elimina questo elemento",
    ja="この項目を削除")
add("删除这个途经点", fr="Supprimer ce point de passage", de="Diesen Via-Punkt löschen",
    es="Eliminar este punto de paso", pt="Excluir este ponto de passagem",
    it="Elimina questo punto di passaggio", ja="この経由点を削除")
add("(未命名轨迹)", fr="(trace sans nom)", de="(Spur ohne Namen)",
    es="(ruta sin nombre)", pt="(trilha sem nome)", it="(tracciato senza nome)",
    ja="(名称未設定の軌跡)")
add("(未命名)", fr="(sans nom)", de="(ohne Namen)", es="(sin nombre)",
    pt="(sem nome)", it="(senza nome)", ja="(名称未設定)")
add("双击定位到这条轨迹的起点", fr="Double-clic : aller au début",
    de="Doppelklick: zum Start", es="Doble clic: ir al inicio",
    pt="Duplo clique: ir ao início", it="Doppio clic: vai all'inizio",
    ja="ダブルクリックで始点へ")
add("双击定位到这个收藏点", fr="Double-clic : aller au favori",
    de="Doppelklick: zum Favoriten", es="Doble clic: ir al favorito",
    pt="Duplo clique: ir ao favorito", it="Doppio clic: vai al preferito",
    ja="ダブルクリックでお気に入りへ")
add("途经点", fr="Points de passage", de="Via-Punkte", es="Puntos de paso",
    pt="Pontos de passagem", it="Punti di passaggio", ja="経由点")
add("面板开着时，点地图即可按顺序添加途经点；路线会依次经过它们。",
    fr="Avec le panneau ouvert, cliquez sur la carte pour ajouter des points de passage dans l'ordre ; l'itinéraire les visite successivement.",
    de="Bei geöffnetem Panel fügt ein Kartenklick Via-Punkte nacheinander hinzu; die Route besucht sie der Reihe nach.",
    es="Con el panel abierto, haz clic en el mapa para añadir puntos de paso en orden; la ruta los visita secuencialmente.",
    pt="Com o painel aberto, clique no mapa para adicionar pontos de passagem em ordem; a rota os visita sequencialmente.",
    it="Con il pannello aperto, clicca sulla mappa per aggiungere punti di passaggio in ordine; il percorso li visita sequenzialmente.",
    ja="パネルを開いた状態で地図をクリックすると順に経由点が追加され、ルートは順番に通過します。")
add("清空", fr="Effacer", de="Löschen", es="Borrar", pt="Limpar", it="Cancella",
    ja="クリア")
add("途经点 %1：%2, %3", fr="Point de passage %1 : %2, %3", de="Via-Punkt %1: %2, %3",
    es="Punto de paso %1: %2, %3", pt="Ponto de passagem %1: %2, %3",
    it="Punto di passaggio %1: %2, %3", ja="経由点 %1：%2, %3")
add("（还没有途经点：面板开着时点地图即可添加；也可先设好起点和终点）",
    fr="(Aucun point de passage : avec le panneau ouvert, cliquez sur la carte ; ou définissez départ et arrivée.)",
    de="(Noch keine Via-Punkte: Panel offen lassen und Karte klicken, oder Start/Ziel setzen.)",
    es="(Aún no hay puntos de paso: con el panel abierto, haz clic en el mapa; o define inicio y fin.)",
    pt="(Ainda não há pontos de passagem: com o painel aberto, clique no mapa; ou defina início e fim.)",
    it="(Ancora nessun punto di passaggio: con il pannello aperto clicca sulla mappa; oppure imposta inizio e fine.)",
    ja="（経由点はまだありません：パネルを開いて地図をクリックするか、始点と終点を設定してください）")
add("全程约 %1 km", fr="Environ %1 km au total", de="Gesamt ca. %1 km",
    es="Unos %1 km en total", pt="Aprox. %1 km no total", it="Totale ca. %1 km",
    ja="全行程約 %1 km")
add("下载地图（全球可下载区域 · Geofabrik）",
    fr="Télécharger la carte (extraits Geofabrik)",
    de="Karte herunterladen (Geofabrik)", es="Descargar mapa (Geofabrik)",
    pt="Baixar mapa (Geofabrik)", it="Scarica mappa (Geofabrik)",
    ja="地図をダウンロード（Geofabrik 配信地域）")
add("可下载区域（✓已下载 ·未下载）",
    fr="Régions téléchargeables (✓ téléchargé · en attente)",
    de="Verfügbare Regionen (✓ heruntergeladen · ausstehend)",
    es="Regiones descargables (✓ descargado · pendiente)",
    pt="Regiões baixáveis (✓ baixado · pendente)",
    it="Regioni scaricabili (✓ scaricate · in attesa)",
    ja="ダウンロード可能地域（✓ 済 · 未）")
add("选择左侧一个地区，查看下载链接并下载。",
    fr="Choisissez une région à gauche pour voir son lien et la télécharger.",
    de="Wählen Sie links eine Region, um den Link zu sehen und zu laden.",
    es="Elige una región a la izquierda para ver su enlace y descargarla.",
    pt="Escolha uma região à esquerda para ver o link e baixá-la.",
    it="Scegli a sinistra una regione per vederne il link e scaricarla.",
    ja="左の地域を選ぶとダウンロードリンクが表示されます。")
add("下载并转换", fr="Télécharger et convertir", de="Herunterladen und konvertieren",
    es="Descargar y convertir", pt="Baixar e converter", it="Scarica e converti",
    ja="ダウンロードして変換")
add("复制下载链接", fr="Copier le lien", de="Link kopieren", es="Copiar enlace",
    pt="Copiar link", it="Copia link", ja="ダウンロードリンクをコピー")
add("同时生成 SRTM 等高线", fr="Générer aussi les courbes SRTM",
    de="Auch SRTM-Höhenlinien erzeugen", es="Generar también curvas SRTM",
    pt="Gerar também curvas SRTM", it="Genera anche curve SRTM",
    ja="SRTM 等高線も生成")
add("请选择最末级地区（带下载链接的）查看详情。",
    fr="Sélectionnez une région terminale (avec lien) pour les détails.",
    de="Wählen Sie eine Endregion (mit Link) für Details.",
    es="Selecciona una región final (con enlace) para los detalles.",
    pt="Escolha uma região final (com link) para detalhes.",
    it="Seleziona una regione finale (con link) per i dettagli.",
    ja="詳細は最下層の地域（リンク付き）を選択。")
add("发现更新", fr="Mise à jour disponible", de="Aktualisierung verfügbar",
    es="Actualización disponible", pt="Atualização disponível", it="Aggiornamento disponibile",
    ja="更新あり")
add("%1  (%2 点)", fr="%1  (%2 points)", de="%1  (%2 Punkte)", es="%1  (%2 puntos)",
    pt="%1  (%2 pontos)", it="%1  (%2 punti)", ja="%1  (%2 点)")
add("导入 GPX 文件", fr="Importer un fichier GPX", de="GPX-Datei importieren",
    es="Importar archivo GPX", pt="Importar arquivo GPX", it="Importa file GPX",
    ja="GPX ファイルをインポート")
add("GPX 轨迹", fr="Traces GPX", de="GPX-Spuren", es="Rutas GPX", pt="Trilhas GPX",
    it="Tracciati GPX", ja="GPX 軌跡")
add("所有文件", fr="Tous les fichiers", de="Alle Dateien", es="Todos los archivos",
    pt="Todos os arquivos", it="Tutti i file", ja="すべてのファイル")
add("导出全部为 GPX", fr="Exporter tout en GPX", de="Alles als GPX exportieren",
    es="Exportar todo a GPX", pt="Exportar tudo para GPX", it="Esporta tutto in GPX",
    ja="すべてを GPX にエクスポート")
add("  ✓已下载", fr="  ✓ téléchargé", de="  ✓ heruntergeladen", es="  ✓ descargado",
    pt="  ✓ baixado", it="  ✓ scaricato", ja="  ✓ 済")
add("  ·未下载", fr="  · en attente", de="  · ausstehend", es="  · pendiente",
    pt="  · pendente", it="  · in attesa", ja="  · 未")
add("  (%1/%2)", fr="  (%1/%2 téléchargé)", de="  (%1/%2 geladen)",
    es="  (%1/%2 descargado)", pt="  (%1/%2 baixado)", it="  (%1/%2 scaricato)",
    ja="  (%1/%2 済)")
add("本地已存在 %1。正在比对远端是否有更新…",
    fr="%1 existe déjà localement. Comparaison avec le serveur…",
    de="%1 liegt lokal vor. Vergleich mit Server…",
    es="%1 ya existe localmente. Comparando con el servidor…",
    pt="%1 já existe localmente. Comparando com o servidor…",
    it="%1 esiste già localmente. Confronto con il server…",
    ja="%1 はローカルに存在します。サーバーと比較中…")
add("下载失败：%1", fr="Échec du téléchargement : %1", de="Download fehlgeschlagen: %1",
    es="Error de descarga: %1", pt="Falha no download: %1", it="Download fallito: %1",
    ja="ダウンロード失敗：%1")
add("下载完成（%1 字节），开始转换…",
    fr="Téléchargement terminé (%1 octets), conversion…",
    de="Download fertig (%1 Bytes), Konvertierung…",
    es="Descarga completa (%1 bytes), conversión…",
    pt="Download concluído (%1 bytes), conversão…",
    it="Download completato (%1 byte), conversione…",
    ja="ダウンロード完了（%1 バイト）、変換中…")
add("运行 sync_maps.sh（转换 + 建地名库 + 地标表）…",
    fr="Exécution de sync_maps.sh (conversion + base de lieux + repères)…",
    de="sync_maps.sh ausführen (Konvertierung + Orts-DB + Orientierungspunkte)…",
    es="Ejecutando sync_maps.sh (conversión + base de lugares + hitos)…",
    pt="Executando sync_maps.sh (conversão + base de lugares + marcos)…",
    it="Esecuzione di sync_maps.sh (conversione + DB luoghi + punti di riferimento)…",
    ja="sync_maps.sh を実行（変換 + 地名DB + ランドマーク表）…")
add("转换完成 ✓ 地图已就绪。", fr="Conversion terminée ✓ carte prête.",
    de="Konvertierung fertig ✓ Karte bereit.", es="Conversión completa ✓ mapa listo.",
    pt="Conversão concluída ✓ mapa pronto.", it="Conversione completata ✓ mappa pronta.",
    ja="変換完了 ✓ 地図の準備完了。")
add("转换失败（exit %1）。可在终端手动运行 scripts/sync_maps.sh 查看详情。",
    fr="Échec de conversion (exit %1). Lancez scripts/sync_maps.sh dans un terminal.",
    de="Konvertierung fehlgeschlagen (exit %1). Führen Sie scripts/sync_maps.sh im Terminal aus.",
    es="Error de conversión (exit %1). Ejecute scripts/sync_maps.sh en la terminal.",
    pt="Falha na conversão (exit %1). Execute scripts/sync_maps.sh no terminal.",
    it="Conversione fallita (exit %1). Esegui scripts/sync_maps.sh nel terminale.",
    ja="変換失敗（exit %1）。ターミナルで scripts/sync_maps.sh を実行してください。")

# ---------------- 右栏按钮 / 提示 ----------------
add("放大", fr="Zoom avant", de="Vergrößern", es="Acercar", pt="Aproximar",
    it="Avvicina", ja="拡大")
add("缩小", fr="Zoom arrière", de="Verkleinern", es="Alejar", pt="Afastar",
    it="Allontana", ja="縮小")
add("定位到关注点", fr="Aller au favori", de="Zum Favoriten", es="Ir al favorito",
    pt="Ir ao favorito", it="Vai al preferito", ja="お気に入りに移動")
add("昼 / 夜 切换", fr="Basculer jour / nuit", de="Tag / Nacht umschalten",
    es="Alternar día / noche", pt="Alternar dia / noite", it="Alterna giorno / notte",
    ja="昼 / 夜 切替")
add("坐标网", fr="Grille", de="Gitter", es="Cuadrícula", pt="Grade", it="Griglia",
    ja="グリッド")
add("显示坐标网", fr="Afficher la grille", de="Gitter anzeigen", es="Mostrar cuadrícula",
    pt="Mostrar grade", it="Mostra griglia", ja="グリッドを表示")
add("轨迹 / 收藏（导入 GPX）", fr="Traces / Favoris (importer GPX)",
    de="Spuren / Favoriten (GPX importieren)", es="Rutas / Favoritos (importar GPX)",
    pt="Trilhas / Favoritos (importar GPX)", it="Tracciati / Preferiti (importa GPX)",
    ja="軌跡 / お気に入り（GPX インポート）")
add("途经点（面板开着时点地图添加）", fr="Points de passage (clic carte si panneau ouvert)",
    de="Via-Punkte (Kartenklick bei offenem Panel)", es="Puntos de paso (clic en mapa con panel abierto)",
    pt="Pontos de passagem (clique no mapa com painel aberto)",
    it="Punti di passaggio (clic sulla mappa con pannello aperto)",
    ja="経由点（パネル開時に地図をクリック）")


def unescape(s):
    # en.xml stores zh with & escaped as &amp;; the runtime loader decodes it back,
    # so the in-memory key matches the literal string used in code (e.g. "菜单(&M)").
    return s.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")


def parse_zh(path):
    txt = open(path, encoding="utf-8").read()
    return [unescape(z) for z in re.findall(r"<zh>(.*?)</zh>", txt, re.S)]


def xml_esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def main():
    zhs = parse_zh(SRC)
    seen = set()
    uniq = []
    for z in zhs:
        if z in seen:
            continue
        seen.add(z)
        uniq.append(z)

    for z in T:
        if z not in seen:
            print("WARN zh key not found in en.xml (typo?):", repr(z), file=sys.stderr)

    lang_meta = [("fr", "Français"), ("de", "Deutsch"), ("es", "Español"),
                 ("pt", "Português"), ("it", "Italiano"), ("ja", "日本語")]

    for code, native in lang_meta:
        out = os.path.join(OUT_DIR, code + ".xml")
        lines = [
            '<?xml version="1.0" encoding="UTF-8"?>',
            f"<!-- Skedaddle 语言包 lang/{code}.xml ({native}) -->",
            "<!-- 由 scripts/gen_lang.py 从翻译表生成；手改会被覆盖。 -->",
            f'<translations lang="{code}">',
        ]
        missing = 0
        for z in uniq:
            d = T.get(z)
            if not d or code not in d:
                missing += 1
                continue
            t = d[code]
            lines.append(f"  <item><zh>{xml_esc(z)}</zh><{code}>{xml_esc(t)}</{code}></item>")
        lines.append("</translations>")
        with open(out, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{code}.xml: {len(uniq) - missing} translated, {missing} fall back to English")


if __name__ == "__main__":
    main()
