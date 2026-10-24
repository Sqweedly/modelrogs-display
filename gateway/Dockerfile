# MODELROGS Display — passerelle (image Alpine, construction en deux étapes)

# --- Étape 1 : installation des bibliothèques --------------------------------
FROM python:3.12-alpine AS construction
COPY requirements.txt /tmp/
# Paquets précompilés pour Alpine (amd64, arm64, armv7) : aucun compilateur requis.
RUN pip install --no-cache-dir --no-compile --prefix=/install -r /tmp/requirements.txt \
 && find /install -depth \( -name "__pycache__" -o -name "tests" -o -name "*.pyi" \) -exec rm -rf {} +

# --- Étape 2 : image finale ---------------------------------------------------
FROM python:3.12-alpine

# Numéro de version, fourni par GitHub Actions à partir du tag (ex. 2.3.0)
ARG VERSION=dev

LABEL org.opencontainers.image.title="modelrogs-gateway" \
      org.opencontainers.image.description="Passerelle des afficheurs MODELROGS Display (SmallTV ESP8266)" \
      org.opencontainers.image.source="https://github.com/sqweedly/modelrogs-display" \
      org.opencontainers.image.version="${VERSION}"

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    CACHE_DIR=/cache \
    PORT=8080 \
    MODELROGS_VERSION=${VERSION}

COPY --from=construction /install /usr/local
COPY app /app/app

RUN adduser -D -H -u 10001 -s /sbin/nologin modelrogs \
 && mkdir -p /cache && chown 10001:10001 /cache

WORKDIR /app
# Pas de « USER » ici : le conteneur démarre en root uniquement pour préparer
# le dossier du cache (création + propriétaire), puis app/__main__.py passe
# définitivement sur l'utilisateur 10001 (ou PUID:PGID) avant de lancer la passerelle.
EXPOSE 8080
VOLUME ["/cache"]

HEALTHCHECK --interval=60s --timeout=5s --start-period=20s --retries=3 \
  CMD wget -q -O /dev/null http://127.0.0.1:8080/health || exit 1

CMD ["python", "-m", "app"]
