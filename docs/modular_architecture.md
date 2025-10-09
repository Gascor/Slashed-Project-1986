# Architecture Modulaire - Slashed Project 1986

## Problème identifié

Les fichiers `game.c` (1639 lignes) et `application.c` (1100 lignes) étaient devenus trop volumineux et difficiles à maintenir, mélangeant de nombreuses responsabilités différentes.

## Solution proposée

### Nouvelle structure modulaire

#### 1. Modules du jeu (`src/game/`)

**`player.c/h`** - Gestion du joueur
- État et mouvement du joueur
- Gestion des collisions
- Système de santé/armure
- View bobbing
- Saut simple et double

**`weapons.c/h`** - Système d'armes
- États des armes (munitions, rechargement, recul)
- Système d'objets d'amélioration d'armes
- Gestion de l'inventaire
- Logique de tir et rechargement

**`world.c/h`** - Monde du jeu
- Gestion des entités du monde
- Types d'entités (joueur, statique, joueur distant)
- Système de création/suppression d'entités

**`hud.c/h`** - Interface utilisateur
- Affichage des statistiques (santé, munitions)
- Réticule dynamique
- Flash de dégâts
- Indicateurs réseau

#### 2. Modules du moteur (`src/core/`)

**`menu.c/h`** - Système de menus
- Navigation dans les menus
- Gestion des écrans (menu principal, options, navigateur de serveurs)
- Caméra de menu
- Transitions entre écrans

#### 3. Modules existants conservés

- `application.c` - Point d'entrée principal (réduit)
- `game.c` - Logique principale du jeu (réduite)
- Tous les autres modules réseau, physique, etc.

## Avantages de cette architecture

### 1. **Séparation des responsabilités**
Chaque module a une responsabilité claire et bien définie.

### 2. **Facilité de maintenance**
- Code plus facile à comprendre et modifier
- Réduction des bugs par isolation des fonctionnalités
- Tests unitaires plus simples à écrire

### 3. **Réutilisabilité**
Les modules peuvent être réutilisés dans d'autres parties du projet.

### 4. **Évolutivité**
Ajout de nouvelles fonctionnalités plus simple sans impacter les autres modules.

### 5. **Collaboration d'équipe**
Plusieurs développeurs peuvent travailler simultanément sur différents modules.

## Plan de migration

### Phase 1 : Création des nouveaux modules ✅
- Création des headers et implémentations de base
- Mise à jour du CMakeLists.txt

### Phase 2 : Migration du code existant
1. Extraire le code joueur de `game.c` vers `player.c`
2. Extraire le code armes de `game.c` vers `weapons.c`
3. Extraire le code monde de `game.c` vers `world.c`
4. Extraire le code HUD de `game.c` vers `hud.c`
5. Extraire le code menu de `application.c` vers `menu.c`

### Phase 3 : Refactorisation
1. Simplifier `game.c` pour qu'il devienne un orchestrateur
2. Simplifier `application.c` pour qu'il se concentre sur la boucle principale
3. Améliorer les interfaces entre modules

### Phase 4 : Tests et optimisations
1. Tests de compilation et de fonctionnement
2. Optimisations de performance si nécessaire
3. Documentation des APIs

## Utilisation

Les nouveaux modules sont maintenant disponibles et peuvent être utilisés dans le code existant :

```c
#include "engine/player.h"
#include "engine/weapons.h"
#include "engine/world.h"
#include "engine/hud.h"
#include "engine/menu.h"
```

## Prochaines étapes recommandées

1. **Migrer le code existant** depuis `game.c` et `application.c` vers les nouveaux modules
2. **Tester la compilation** après chaque migration
3. **Ajouter des tests unitaires** pour chaque module
4. **Documenter les APIs** de chaque module
5. **Considérer d'autres modules** si des fichiers deviennent trop gros (renderer, network, etc.)

Cette architecture modulaire offre une base solide pour le développement futur du projet tout en maintenant les performances et la lisibilité du code.