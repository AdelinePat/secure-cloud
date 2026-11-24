# SecureChat — Intégration E2EE (prototype étudiant)

## 1. État de livraison

Base analysée : `SecureChat-review.zip`, export Git du 23 novembre 2026. Cette archive est antérieure aux dernières modifications locales de MainWindow effectuées ensemble : elle contient encore `testSessionKey_` et ne contient pas le dialogue d'identité. Le patch réintègre ces fonctionnalités et ajoute le chemin E2EE. Il ne faut pas écraser aveuglément des modifications locales plus récentes.

Livrables : `SecureChat-E2EE.patch` (diff Git) et `SecureChat-E2EE-fichiers.zip` (huit fichiers modifiés, sans copie des configurations ou certificats). Aucun changement n'a été effectué sur ton dépôt local ou distant.

**Validation :** `git diff --check` a réussi. Le code a été inspecté statiquement, mais je n'ai pas pu compiler ni exécuter les tests : Qt6, libsodium et libpqxx de développement ne sont pas disponibles dans cet environnement. Le résultat est une implémentation candidate, pas une version certifiée fonctionnelle.

## 2. Architecture réelle

Le client Qt utilise `HttpClient`, `ConversationService` et `MessageService`. Le Gateway Boost.Beast écoute sur 8080 et transmet les requêtes à Auth et Messaging. Auth assure login et refresh JWT ES256. Messaging vérifie les JWT, gère les participants et persiste les messages chiffrés. Auth et Messaging utilisent la même base PostgreSQL (`app-db`). Audit et File ne sont pas modifiés.

Contrairement à une hypothèse précédente, les routes du snapshot sont `/auth/login`, `/auth/refresh` et `/messaging/conversations`, et non `/secureChat/users`. Auth ne fournit pas d'API utilisateur générique dans ce snapshot.

## 3. Modifications apportées

| Fichier | Rôle |
|---|---|
| `messaging/src/main.cpp` | Publication authentifiée de clé publique, récupération de la clé du correspondant avec contrôle de participation, prise en charge de la version 2 des messages |
| `gateway/src/HttpRouter.cpp` | Routage des deux nouveaux endpoints, conservation du Bearer JWT |
| `client/include/network/HttpClient.hpp` | Déclarations des deux méthodes HTTP |
| `client/src/network/HttpClient.cpp` | Implémentation PUT/GET et sérialisation JSON |
| `client/include/ui/MainWindow.hpp` | Identité optionnelle et clés par conversation |
| `client/src/ui/MainWindow.cpp` | Création/chargement de l'identité, publication, dérivation, envoi, réception, nettoyage et rendu texte |
| `auth/tests/seed_test_user.cpp` | Choix entre alice_test et bob_test par argument |
| `client/tests/ui/test_MainWindow.cpp` | Adaptation du serveur HTTP simulé et des dialogues de test |

Pour gagner du temps, la publication est dans Messaging, qui possède déjà un vérificateur JWT et accède à la table users. Ce choix évite de dupliquer le vérificateur dans Auth. Dans une architecture plus stricte, on déplacerait la gestion des identités publiques dans Auth ou un service dédié.

## 4. Contrats HTTP

### Publication

`PUT /messaging/identity-key` (Gateway), relayé vers `/identity-key`.

Authorization: Bearer JWT. JSON : `{"public_key":"<32 octets encodés en Base64>"}`.

Le sujet du JWT détermine l'utilisateur ; le client ne fournit pas d'identifiant à modifier. Une clé vide en base peut être renseignée ; une nouvelle publication identique est acceptée. Une clé différente est refusée (409). Une clé invalide retourne 400. Un JWT invalide retourne 401.

### Récupération

`GET /messaging/conversations/{id}/peer-key`, relayé vers `/conversations/{id}/peer-key`.

Réponse 200 : `{"user_id":"<UUID du correspondant>","public_key":"<Base64>"}`. Le serveur vérifie l'appartenance à la conversation et exige exactement deux participants. Une clé absente ou un groupe de taille différente entraîne 409. Un non-membre reçoit 403.

## 5. Cryptographie expliquée

Chaque utilisateur génère une paire X25519 avec `crypto_kx_keypair`. La clé privée reste sur l'appareil. IdentityKeyStore chiffre la paire sur disque avec une clé dérivée par Argon2id et la passphrase. La clé publique seule est publiée en Base64 (encodage, pas chiffrement).

Pour chaque conversation, les deux clients utilisent un ordre stable des UUID : le plus petit joue le rôle client de libsodium, l'autre le rôle serveur. Ces rôles ne sont pas ceux des microservices HTTP. `crypto_kx_client_session_keys` et `crypto_kx_server_session_keys` produisent deux clés directionnelles : Alice.tx = Bob.rx et Alice.rx = Bob.tx. Une dérivation BLAKE2b avec l'identifiant de conversation sépare les clés de conversations différentes.

À l'envoi, `crypto_secretbox_easy` chiffre le texte UTF-8 avec tx et un nonce aléatoire de 24 octets. Le serveur stocke nonce, ciphertext, auteur et version. À la réception, `crypto_secretbox_open_easy` vérifie le MAC puis déchiffre avec rx pour les messages du correspondant, tx pour ses propres messages historiques. Si la vérification échoue, le texte n'est pas affiché.

La version 2 identifie les nouveaux messages. Les anciens messages version 1 utilisaient une clé aléatoire temporaire : ils restent illisibles, ce qui est normal. Le serveur accepte les deux versions pour compatibilité, mais le nouveau client n'envoie que la version 2.

La clé de conversation n'est jamais transmise au serveur. Le bouton Send reste désactivé si la clé du correspondant n'est pas disponible. Le rafraîchissement des conversations permet de relancer la récupération après la première connexion de Bob. L'historique est chargé lors de la sélection ou du rafraîchissement ; ce patch n'ajoute pas de WebSocket ni de polling temps réel.

## 6. Intégration sans perdre ton travail

Depuis Git Bash à la racine du dépôt :

```bash
git status
git switch -c feat/e2ee-integration
```

Sauvegarde tes modifications locales dans un commit de travail avant l'intégration (ne commite aucun secret). Copie le patch hors du dépôt ou utilise son chemin absolu :

```bash
git apply --check /chemin/SecureChat-E2EE.patch
```

Si la vérification réussit :

```bash
git apply /chemin/SecureChat-E2EE.patch
```

Si elle échoue sur MainWindow.cpp ou MainWindow.hpp, c'est attendu si les modifications de la conversation sont plus récentes que le ZIP. Compare les fichiers du ZIP de livraison avec tes fichiers locaux et fusionne les changements. Ne force pas le patch et ne réinitialise pas ton dépôt. Le ZIP contient uniquement les fichiers modifiés, pas un dépôt complet.

## 7. Compilation et création de Bob

Conserve tes volumes PostgreSQL et tes identités locales. Ne fais pas `docker compose down -v`.

Le compose monte les sources des services dans les conteneurs. Recompile/redémarre les services modifiés selon ton workflow `secureChat.sh` ; le script est à inspecter si les exécutables ne sont pas reconstruits automatiquement. Exemple :

```bash
docker compose restart gateway messaging auth
docker compose logs --tail=50 gateway messaging auth
cmake --build client/build
```

Si les services ne recompilent pas automatiquement, exécute dans les conteneurs les commandes CMake correspondantes (par exemple `docker compose exec auth cmake --build /app/auth/build`). Vérifie les chemins de build et les logs avant de poursuivre.

L'utilitaire `seed_test_user` accepte désormais `bob_test`. Dans le conteneur auth, après compilation, utilise un mot de passe de test via une variable d'environnement temporaire, sans l'écrire dans le code :

```bash
docker compose exec auth bash
read -rsp 'Bob test password: ' AUTH_TEST_PASSWORD; echo
export AUTH_TEST_PASSWORD
/app/auth/build/bin/seed_test_user bob_test
unset AUTH_TEST_PASSWORD
exit
```

Si Bob existe déjà, ne relance pas l'insertion. Le repository impose l'unicité du username/email.

## 8. Recette manuelle prioritaire

1. Démarrer Alice et Bob avec deux instances du client, chacun avec son propre compte. Déverrouiller Alice ; créer l'identité de Bob. Vérifier que les deux publications retournent 200.
2. Alice crée une conversation avec le seul participant `bob_test`. Bob rafraîchit sa liste et sélectionne cette conversation. Si la clé n'est pas encore disponible, rafraîchir après la connexion de l'autre utilisateur.
3. Alice envoie `Bonjour Bob ! éè 😀`. Bob rafraîchit les conversations : le texte doit apparaître. Bob répond ; Alice rafraîchit.
4. Fermer les deux clients, relancer et vérifier le déchiffrement de l'historique version 2. Vérifier en base que ciphertext ne contient pas le texte et que la version vaut 2.
5. Tester une mauvaise passphrase, un JWT invalide, une tentative de remplacement de clé (409), une conversation de plus de deux participants (Send désactivé), et une requête peer-key effectuée par un non-membre (403).
6. Exécuter les tests du client avec `ctest --test-dir client/build --output-on-failure`. Le test UI a été adapté mais doit être exécuté ; il peut nécessiter des ajustements du serveur simulé ou du timing Qt. Exécuter aussi les suites auth, gateway et messaging dans leur environnement habituel.

### Si une clé publique est déjà renseignée

Les comptes de seed peuvent contenir des valeurs factices. La publication refuse volontairement d'écraser une clé existante différente. Vérifie les données de test en base avant toute correction. Ne remets à vide une clé que pour un compte de test dont tu maîtrises l'identité et dont tu acceptes de perdre la capacité à déchiffrer les anciens messages. Ne supprime jamais une identité locale réelle sans sauvegarde.

## 9. Limites à présenter au jury

- Prototype à deux utilisateurs, un appareil par utilisateur. Pas de chiffrement de groupe ni de pièces jointes E2EE dans ce patch.
- Le serveur est considéré fiable pour la distribution initiale des clés. Il pourrait substituer une clé publique : il manque une vérification d'empreintes indépendante ou un mécanisme de confiance équivalent.
- Les clés statiques ne fournissent pas de forward secrecy ni de Double Ratchet. Une compromission de clé privée peut exposer l'historique.
- Le chiffrement authentifié ne protège pas toutes les métadonnées ni contre le rejeu. Le serveur voit les identifiants, horaires et tailles.
- Le transport HTTP local n'est pas une configuration de production : TLS est nécessaire pour les JWT et la distribution des clés.
- Nettoyage mémoire partiel via sodium_memzero ; les copies temporaires Qt/C++ ne sont pas toutes garanties effacées.
- Pas de rotation/récupération des clés ; une identité locale perdue doit être traitée explicitement. Pas de garantie de sécurité contre un serveur actif malveillant.

## 10. Budget restant (objectif 10 heures)

| Tâche | Budget |
|---|---:|
| Fusion du patch et compilation client/backend | 2 h |
| Création Bob et vérification des endpoints | 1 h |
| Recette bidirectionnelle, historique, UTF-8 | 2 h |
| Corrections de compilation, tests UI et cas d'erreur | 3 h |
| Documentation finale et préparation de démonstration | 2 h |
| Total cible | 10 h |

Ce budget est un objectif, non une garantie : aucun build complet ni test Docker n'a pu être exécuté ici. Si les interfaces de ton dépôt local diffèrent du snapshot, la fusion peut prendre plus de temps. Priorité absolue : démonstration Alice/Bob, historique et absence de texte clair en base. Reporter les groupes, pièces jointes, suppression de passphrase et refonte d'architecture.
