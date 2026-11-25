# Audit de sécurité du backend SecureChat

## Périmètre et preuves

Revue statique de la branche `feat/client-e2ee`, à partir de `fd563fa`. Le périmètre couvre les échanges HTTP entre Gateway, Auth et Messaging, l’émission et la vérification des JWT, les contrôles d’accès, les clés publiques et l’historique PostgreSQL. La revue du client Qt6 se limite aux interfaces utilisées par le backend et aux principes d’E2EE et de TOFU visibles dans les sources.

Les défauts confirmés ci-dessous sont établis par lecture du code. Aucun résultat d’exploitation en environnement d’exécution n’a été établi pendant cette intervention.

| ID | Criticité | Qualification et état |
| --- | --- | --- |
| SEC-01 | Haute | Défaut confirmé par le flux de contrôle. Une erreur de lecture ou d’écriture pouvait atteindre le gestionnaire fatal du serveur. Messaging utilise maintenant les surcharges à code d’erreur de Beast et ferme la connexion concernée. Gateway et Messaging acceptent les corps jusqu’à 2 Mio et renvoient 413 au-delà. Le plafond du texte chiffré décodé reste à 1 Mio. |
| SEC-02 | Moyenne | Défaut confirmé. Le motif de la route de clé du correspondant acceptait des chaînes de 36 caractères dont les tirets pouvaient être mal placés. Les routes de lecture, d’envoi et de clé utilisent désormais le même contrôle du format UUID. Avec une authentification valide, un identifiant invalide renvoie 400 avant accès à PostgreSQL. |
| SEC-03 | Moyenne | Architecture confirmée, risque de disponibilité. Messaging traite une connexion à la fois avec des entrées et sorties synchrones. Une connexion lente ou une opération PostgreSQL lente peut retarder les suivantes. |
| SEC-04 | Haute en production | Absence de TLS confirmée sur les liaisons HTTP examinées. Les connexions applicatives vers Gateway et les relais Gateway vers Auth et Messaging utilisent HTTP ou des sockets sans TLS. Les identifiants de connexion, jetons et métadonnées requièrent une protection en transit. Le canal d’audit configuré en HTTPS relève d’une liaison distincte. |
| SEC-05 | Faible | Défaut confirmé et corrigé. Les réponses 405 de Gateway annoncent `PUT` pour la clé d’identité, `GET` pour la clé du correspondant et `GET, POST` pour les conversations et messages. |
| SEC-06 | Moyenne | Contrôle incomplet confirmé. Auth et Messaging exigent maintenant un UUID avec cinq groupes de 8, 4, 4, 4 et 12 chiffres hexadécimaux. Messaging ramène le sujet à la casse de sortie de PostgreSQL avant les comparaisons. |
| SEC-07 | Moyenne | Audience absente confirmée. Auth émet `aud` avec la valeur `securechat-api` et les deux validateurs exigent cette valeur sous forme de chaîne. La recherche des consommateurs a identifié la vérification dans Messaging, le validateur d’Auth et le transport du jeton par Gateway et le client Qt6. |
| SEC-08 | Moyenne | Limite confirmée. Messaging valide localement les access tokens et ignore l’état des sessions Auth. La rotation du refresh token révoque la session précédente dans Auth. Un access token émis avant cette rotation peut rester utilisable jusqu’à expiration. |
| SEC-09 | Faible | Durcissement. Chaque segment doit être intégralement décodé en Base64URL sans remplissage. La taille du JWT est plafonnée à 4 096 octets avant découpe. Aucun contournement préalable n’a été établi. |
| SEC-10 | Moyenne | Défaut confirmé par lecture du code. Une limite de zéro pouvait produire une page vide et conduire à un appel à `back()`. Le dépôt refuse désormais les valeurs hors de 1 à 100 avant d’ouvrir une transaction et protège explicitement l’accès à la dernière ligne. |
| SEC-11 | Faible | Risque potentiel de TOCTOU. Certains contrôles d’appartenance et les lectures ou écritures suivantes sont réalisés dans des transactions distinctes. Si l’appartenance change entre les deux opérations, la décision peut reposer sur l’état précédent. |
| SEC-12 | Moyenne | Limitation confirmée. La route HTTP demande les 50 messages les plus récents. Elle renvoie un `next_cursor` éventuel, mais n’accepte pas de paramètre de pagination. Le dépôt sait lire la page suivante avec un curseur et une limite maximale de 100. |

## Déploiement des JWT

Déployer Auth et Messaging ensemble. Après le déploiement, renouveler les anciens access tokens avec le refresh token valide ou se reconnecter. Les access tokens antérieurs dépourvus de l’audience exigée sont rejetés. Le format de signature ES256, l’émetteur `securechat-auth` et la durée d’émission de 900 secondes sont conservés.

## Limites et suites possibles

- Limiter l’exposition du HTTP examiné et déployer TLS sur les liaisons qui transportent des identifiants ou des jetons. L’E2EE des messages couvre leur contenu chiffré. Les jetons, les routes et les métadonnées nécessitent leur propre protection.
- Le calcul des clés E2EE examiné utilise les paires d’identité durables et ne fournit pas de confidentialité persistante (*Perfect Forward Secrecy*). Une compromission ultérieure d’une clé privée d’identité peut mettre en jeu les messages anciens enregistrés pour cette paire.
- TOFU permet de détecter un changement après la première clé mémorisée. L’authenticité de cette première clé repose sur le premier contact, sauf comparaison des empreintes par un canal indépendant.
- La route de clé du correspondant exige exactement deux participants. Cette interface ne fournit pas les échanges de clés nécessaires à l’E2EE d’un groupe à plus de deux participants. La prise en charge complète des groupes et leur comportement côté client restent hors de la validation de cette intervention.
- Des délais d’entrée et de sortie et un traitement concurrent borné réduiraient le risque lié au serveur synchrone. Une consultation de l’état des sessions ou une liste de révocation côté Messaging permettrait d’envisager la révocation immédiate. Regrouper contrôle d’appartenance et opérations protégées dans une même transaction ou requête réduirait la fenêtre TOCTOU.
- Exposer et valider le curseur côté HTTP serait nécessaire pour parcourir les messages plus anciens. Le test du dépôt couvre une page de 100 messages et l’utilisation du curseur sur une seconde page.
- L’infrastructure de production, les certificats déployés, la configuration réseau réelle, les services File et Audit en profondeur, les performances en charge, les dépendances et le stockage local du client n’ont pas fait l’objet d’une validation approfondie.

## Tests ajoutés et résultats

- **Auth**. Jeton signé valide, audience absente ou incorrecte et sujet non UUID. Les fixtures JWT utilisent des UUID et les chemins des clés de test sont déterminés par CMake.
- **Messaging**. Jeton valide, UUID canonisé, signature modifiée ou malformée, mauvaise clé, expiration, émission dans le futur, émetteur, audience, sujet, algorithme, Base64URL et taille. Le test PostgreSQL utilise une table temporaire pour contrôler les limites, les pages vides et le curseur.
- **Gateway**. Réponses 405 et valeurs de `Allow` selon la route.
- **Intégration HTTP**. Environnement Docker Compose isolé, PostgreSQL sur stockage temporaire, clés de signature éphémères et trois comptes de test. Le scénario couvre les 401 et 403, l’isolation des listes, l’expéditeur déterminé par le JWT, l’immuabilité de la clé d’identité, les UUID invalides, les réponses 405 et l’état de santé. Le scénario d’une requête HTTP malformée suivie d’un contrôle d’état n’est pas couvert.
- **Exécuté**. `git diff --check`, validation de la configuration Compose de test, chargement du script Python via `--help` et compilation puis exécution d’une vérification autonome C++20 du format UUID.
- **Bloqué**. CMake sous Windows 11 avec GCC 15.2 et Ninja n’a pas trouvé les en-têtes et la bibliothèque de développement PostgreSQL pour Auth et Messaging, ni les en-têtes Boost pour Gateway. Git Bash n’a pas démarré dans la session, avec le code Windows `0xC0000142`. Docker Desktop a refusé l’accès à `dockerDesktopLinuxEngine` au démarrage de la suite. Les services n’ont pas été compilés et aucune suite GTest ou HTTP n’a été exécutée.

## Commandes de validation

Sous Windows 11, depuis PowerShell et à la racine du dépôt, avec Python 3 et Docker Desktop en mode conteneurs Linux.

```powershell
git status --short --branch
git diff --check
docker info
docker compose -f tests/security/compose.yml config --quiet
python tests/security/http_security.py
```

Depuis Git Bash, à la racine du dépôt sous Windows 11.

```bash
bash -n tests/security/start-backend.sh
docker compose -f tests/security/compose.yml config --quiet
python tests/security/http_security.py
```

Le script lance un projet Compose au nom unique, configure et compile Auth, Messaging et Gateway avec Ninja, exécute CTest, puis vérifie les routes HTTP et arrête le projet isolé. Le test PostgreSQL reçoit automatiquement l’adresse de la base de test. Seul Gateway est publié, sur un port local aléatoire. Les valeurs d’authentification de test sont générées à l’exécution et les sorties du script n’affichent ni mot de passe, ni JWT, ni clé privée.
