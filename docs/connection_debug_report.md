# 🔧 Résolution du Problème de Déconnexion - Rapport de Debug

## 🔍 Problème Identifié

**Symptôme :** Le client se déconnecte instantanément du serveur après connexion, avec ce pattern répétitif :
```
[network] client connected (1/8)
[network] client disconnected (0/8)
```

## 🕵️ Investigation et Causes Découvertes

### 1. Problème d'Interface Utilisateur ❌ **CRITIQUE**
- **Cause :** La fonction `ui_button()` déclenchait l'action sur **DEUX frames** :
  - Frame 1: `input->mouse_left_pressed` = true → Action
  - Frame 2: `input->mouse_left_released` = true → Action encore
- **Effet :** Spam continu de tentatives de connexion
- **Code problématique :**
```c
bool pressed = hovered && input && (input->mouse_left_pressed || input->mouse_left_released);
```

### 2. Problème de Port/Serveur ⚠️ **IMPORTANT**
- **Observation :** Le client essaie de se connecter au mauvais serveur :
  - Tente: `Specter Woods (203.0.113.12:26015)` ❌
  - Tente: `Basilisk Stronghold (127.0.0.1:26015)` ❌ (mauvais port)
  - Devrait: `Serveur Local (127.0.0.1:27015)` ✅
- **Cause probable :** Problème dans la liste des serveurs du master server

### 3. Logique de Connexion ⚠️ **À INVESTIGUER**
- La logique de reconnexion automatique peut causer une boucle infinie
- `app.pending_join` se remet à `true` à chaque frame si la connexion échoue

---

## ✅ Corrections Appliquées

### 1. Correction de l'UI Button
```c
// AVANT:
bool pressed = hovered && input && (input->mouse_left_pressed || input->mouse_left_released);

// APRÈS:
bool pressed = hovered && input && input->mouse_left_pressed;
```

### 2. Messages de Debug Ajoutés
- **Serveur :** Messages détaillés pour les connexions/déconnexions
- **Client :** Messages pour les étapes de connexion
- **UI :** Debug des clics de bouton

---

## 🧪 Tests en Cours

### Configuration Test:
- **Serveur :** `127.0.0.1:27015` (avec debug activé)
- **Client :** Avec debug UI activé
- **Master Server :** `127.0.0.2:27050`

### Messages de Debug Attendus:
**Séquence de connexion normale :**
1. `[debug] Join button clicked` (client UI)
2. `[network_client] connected to server, sending hello` (client réseau)
3. `[network] client connected - waiting for hello message` (serveur)
4. `[network] received message type: 0x01` (serveur reçoit HELLO)
5. `[network] received hello, sending welcome` (serveur envoie WELCOME)
6. `[network_client] received message type: 0x02` (client reçoit WELCOME)
7. `[network_client] received welcome message, connection established` (succès)

---

## 🎯 Prochaines Étapes

### Si le problème UI persiste :
1. Investiguer l'état de `input->mouse_left_pressed`
2. Vérifier si l'état reste `true` plusieurs frames
3. Implémenter un système de "debounce" pour les boutons

### Si le problème de port persiste :
1. Vérifier la configuration du master server
2. S'assurer que notre serveur s'annonce correctement
3. Permettre la connexion directe sans passer par le master

### Test de Connexion Manuelle :
1. Lancer le client
2. Aller dans "Join A Server"
3. **UN SEUL CLIC** sur "Join Selected" pour le serveur local
4. Observer les messages de debug

---

## 📊 État Actuel

- ✅ **Serveur :** Démarré et en écoute sur 27015
- ✅ **Client :** Compilé avec corrections UI
- ✅ **Debug :** Messages activés des deux côtés
- 🔄 **Test :** En attente de test manuel prudent

*Prêt pour test de connexion unique et analyse des logs de debug.*