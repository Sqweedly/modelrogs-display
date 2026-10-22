"""Appels HTTP sortants (client aiohttp partagé)."""

import aiohttp


async def get_json(session: aiohttp.ClientSession, url: str, params: dict | None = None,
                   headers: dict | None = None):
    propres = {k: str(v) for k, v in (params or {}).items()}
    async with session.get(url, params=propres, headers=headers) as reponse:
        reponse.raise_for_status()
        return await reponse.json(content_type=None)
