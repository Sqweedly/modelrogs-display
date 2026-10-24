"""Point d'entrée du conteneur.

Le conteneur démarre en root le temps de préparer le dossier du cache
(chemin de l'hôte monté sur /cache, souvent créé par Docker et appartenant
à root), puis abandonne définitivement root avant de lancer la passerelle.
"""

import os
import sys


def _log(message: str) -> None:
    print(f"[demarrage] {message}", file=sys.stderr, flush=True)


def _preparer_cache(dossier: str, uid: int, gid: int) -> None:
    """Crée le dossier du cache et le donne (avec son contenu) à uid:gid."""
    try:
        os.makedirs(dossier, exist_ok=True)
        corriges = 0
        for racine, dossiers, fichiers in os.walk(dossier):
            for chemin in [racine] + [os.path.join(racine, n) for n in dossiers + fichiers]:
                st = os.lstat(chemin)
                if (st.st_uid, st.st_gid) != (uid, gid):
                    os.lchown(chemin, uid, gid)
                    corriges += 1
        if corriges:
            _log(f"Dossier du cache {dossier} : propriétaire {uid}:{gid} appliqué ({corriges} élément(s))")
    except OSError as exc:
        _log(f"Dossier du cache {dossier} non préparé ({exc}). "
             f"Vérifiez « cap_add: CHOWN » dans docker-compose.yml, "
             f"ou lancez sur l'hôte : sudo chown -R {uid}:{gid} <dossier du cache>")


def _abandonner_root(uid: int, gid: int) -> None:
    try:
        os.setgroups([])
        os.setgid(gid)
        os.setuid(uid)
    except OSError as exc:
        _log(f"Impossible de quitter le compte root ({exc}) : arrêt par sécurité. "
             "Ajoutez « cap_add: [CHOWN, SETUID, SETGID] » dans docker-compose.yml.")
        sys.exit(1)


if os.getuid() == 0:
    UID = int(os.environ.get("PUID", "10001"))
    GID = int(os.environ.get("PGID", "10001"))
    _preparer_cache(os.environ.get("CACHE_DIR", "/cache"), UID, GID)
    _abandonner_root(UID, GID)

from .main import main  # noqa: E402 — importé une fois root abandonné

main()
