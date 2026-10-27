# Icônes incluses dans l'image de la passerelle

Converties en PNG 128 × 128 (fond transparent) pour l'afficheur.

| Dossier | Source | Licence |
|---|---|---|
| `devises/` | drapeaux carrés de [lipis/flag-icons](https://github.com/lipis/flag-icons) | MIT |
| `commodities/` (petrole, gaz, ble, mais, soja, cafe, cacao) | [microsoft/fluentui-emoji](https://github.com/microsoft/fluentui-emoji), style « Flat » | MIT |
| `commodities/` (or, argent, cuivre) | lingots de [nvstly/icons](https://github.com/nvstly/icons) (cuivre : lingot d'argent recoloré) | aucune licence déclarée |
| `actions/` | logos de [nvstly/icons](https://github.com/nvstly/icons) (fond sombre retiré pour Nvidia et Tesla) | aucune licence déclarée ; les logos restent la propriété des sociétés |

Les logos d'actions viennent d'abord de [Logo.dev](https://logo.dev) quand la clé
`LOGO_DEV_TOKEN` est renseignée dans `docker-compose.yml` ; ceux-ci servent de
secours. Logos provided by Logo.dev.

**Ajouter ou remplacer une icône** : déposer un PNG (fond transparent, 128 px ou
plus) dans le bon dossier, nommé comme suit, puis publier une nouvelle version :
- `actions/<SYMBOLE YAHOO>.png` (ex. `actions/MC.PA.png`) ;
- `devises/<CODE>.png` (ex. `devises/KRW.png`) ;
- `commodities/<nom>.png`, nom indiqué dans `app/modules/cotations.py` (table `COMMODITIES`).
