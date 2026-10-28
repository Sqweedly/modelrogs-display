# Icônes incluses dans l'image de la passerelle

Converties en PNG 128 × 128 (fond transparent) pour l'afficheur.

| Dossier | Source | Licence |
|---|---|---|
| `devises/` | pastilles générées par `gateway/tools/icones_devises.py` (disque de couleur et symbole de la monnaie), avec les polices [Inter](https://github.com/rsms/inter) et [Noto Serif CJK](https://github.com/notofonts/noto-cjk) (pour « 元 ») | SIL Open Font License 1.1 (polices) ; les pastilles font partie du projet |
| `commodities/` (petrole, gaz, ble, mais, soja, cafe, cacao) | [microsoft/fluentui-emoji](https://github.com/microsoft/fluentui-emoji), style « Flat » | MIT |
| `commodities/` (or, argent, cuivre) | lingots de [nvstly/icons](https://github.com/nvstly/icons) (cuivre : lingot d'argent recoloré) | aucune licence déclarée |
| `actions/` | logos de [nvstly/icons](https://github.com/nvstly/icons) (fond sombre retiré pour Nvidia et Tesla) | aucune licence déclarée ; les logos restent la propriété des sociétés |

Les logos d'actions viennent d'abord de [Logo.dev](https://logo.dev) quand la clé
`LOGO_DEV_TOKEN` est renseignée dans `docker-compose.yml` ; ceux-ci servent de
secours. Logos provided by Logo.dev.

**Sans toucher à l'image** : chaque utilisateur peut remplacer ou ajouter des
icônes chez lui, dans un dossier monté sur `/icones` (voir « Icônes
personnelles » dans `docs/PASSERELLE.md`). Rien n'y est redistribué.

**Ajouter ou remplacer une icône incluse** (pour tout le monde) : déposer un PNG
(fond transparent, 128 px ou plus) dans le bon dossier, nommé comme suit, puis
publier une nouvelle version :
- `actions/<SYMBOLE YAHOO>.png` (ex. `actions/MC.PA.png`) ;
- `devises/<CODE>.png` : ajouter la devise dans `gateway/tools/icones_devises.py`
  (symbole et couleurs) et relancer le script, qui régénère toutes les pastilles ;
- `commodities/<nom>.png`, nom indiqué dans `app/modules/cotations.py` (table `COMMODITIES`).

Seules des icônes dont la licence permet la redistribution peuvent être incluses
(MIT, OFL, domaine public…).
