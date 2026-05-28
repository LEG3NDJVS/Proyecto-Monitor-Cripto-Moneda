import json
import paho.mqtt.client as mqtt
import requests

# ── CONFIGURACION ─────────────────────────────────────────────
TELEGRAM_TOKEN   = ""
TELEGRAM_CHAT_ID = ""  # Canal publico de Telegram
MQTT_BROKER      = ""              # IP del PC donde corre Mosquitto
MQTT_PORT        = 1883                        # Puerto estandar de MQTT

# Umbrales de precio que disparan la alarma de Telegram
umbral_btc = 100000  # Alarma si BTC supera este valor en USD
umbral_sol = 200     # Alarma si SOL supera este valor en USD

# Precios anteriores para detectar si el precio subio, bajo o se mantuvo
precio_ant_btc = 0
precio_ant_sol = 0

def enviar_telegram(mensaje):
    """Envia un mensaje de texto al canal de Telegram configurado."""
    url = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage"
    data = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": mensaje,
        "parse_mode": "Markdown"  # Permite texto en negrita con *asteriscos*
    }
    try:
        requests.post(url, data=data, timeout=5)
        print(f"Telegram enviado: {mensaje[:50]}...")
    except Exception as e:
        print(f"Error enviando Telegram: {e}")

def notificar_precio(moneda, precio, precio_ant, umbral):
    """
    Analiza el cambio de precio y envia la notificacion correspondiente
    a Telegram segun si el precio subio, bajo o se mantuvo igual.
    """
    # Nombre completo de la moneda para mostrar en el mensaje
    nombre = "₿ Bitcoin (BTC)" if moneda == "BTC" else "◎ Solana (SOL)"

    # Primera vez que llega el precio: solo mostrar info inicial
    if precio_ant == 0:
        enviar_telegram(
            f"🚀 *{nombre}*\n"
            f"\n"
            f"💰 Precio actual: *${precio:,.2f}*\n"
            f"🔔 Umbral de alarma: *${umbral:,.2f}*\n"
            f"\n"
            f"📡 Monitoreando en tiempo real..."
        )
        return

    # Calcular cuanto cambio el precio respecto al anterior
    cambio     = precio - precio_ant
    porcentaje = (cambio / precio_ant) * 100

    if cambio > 0:
        # Precio subio: notificar y sugerir vender
        enviar_telegram(
            f"📈 *{nombre} — PRECIO EN ALZA*\n"
            f"\n"
            f"💰 Precio actual: *${precio:,.2f}*\n"
            f"⬆️ Subio: +${cambio:,.2f} (+{porcentaje:.3f}%)\n"
            f"\n"
            f"💡 Considera vender si llegaste a tu objetivo."
        )
    elif cambio < 0:
        # Precio bajo: notificar y sugerir comprar
        enviar_telegram(
            f"📉 *{nombre} — BUENA HORA DE INVERTIR*\n"
            f"\n"
            f"💰 Precio actual: *${precio:,.2f}*\n"
            f"⬇️ Bajo: -${abs(cambio):,.2f} ({porcentaje:.3f}%)\n"
            f"\n"
            f"🛒 El precio cayo, puede ser un buen momento para comprar."
        )
    else:
        # Precio igual al anterior: notificar estabilidad
        enviar_telegram(
            f"➡️ *{nombre} — PRECIO ESTABLE*\n"
            f"\n"
            f"💰 Precio actual: *${precio:,.2f}*\n"
            f"\n"
            f"😐 Sin cambios desde la ultima actualizacion."
        )

    # Verificar si el precio supero el umbral configurado
    if precio >= umbral:
        enviar_telegram(
            f"🚨 *ALERTA DE UMBRAL — {moneda}*\n"
            f"\n"
            f"⚠️ El precio supero el limite configurado\n"
            f"💰 Precio: *${precio:,.2f}*\n"
            f"🔔 Umbral: *${umbral:,.2f}*\n"
            f"\n"
            f"⚡️ Toma una decision rapido."
        )

def on_connect(client, userdata, flags, rc):
    """Se ejecuta automaticamente cuando el cliente se conecta al broker MQTT."""
    if rc == 0:
        print("Conectado al broker MQTT")
        # Suscribirse a los topicos de precios y configuracion
        client.subscribe("cripto/precios/btc")
        client.subscribe("cripto/precios/sol")
        client.subscribe("cripto/config/monitor01")
        # Notificar en Telegram que el monitor inicio correctamente
        enviar_telegram(
            f"✅ *Monitor de Criptomonedas iniciado*\n"
            f"\n"
            f"📊 Monitoreando BTC y SOL en tiempo real\n"
            f"\n"
            f"📈 Subida: te aviso para considerar vender\n"
            f"📉 Bajada: te aviso que es buena hora de invertir\n"
            f"🚨 Umbral: te aviso si supera el limite configurado"
        )
    else:
        print(f"Error de conexion: {rc}")

def on_message(client, userdata, msg):
    """Se ejecuta automaticamente cada vez que llega un mensaje MQTT."""
    global precio_ant_btc, precio_ant_sol, umbral_btc, umbral_sol

    topico  = msg.topic
    mensaje = msg.payload.decode("utf-8")  # Convertir bytes a texto

    # Mensaje de configuracion enviado desde el Configurador Java
    if topico == "cripto/config/monitor01":
        config = json.loads(mensaje)
        # Actualizar umbrales con los nuevos valores recibidos
        umbral_btc = config.get("umbral_btn1", umbral_btc)
        umbral_sol = config.get("umbral_btn2", umbral_sol)
        print(f"Configuracion actualizada: BTC=${umbral_btc} SOL=${umbral_sol}")
        # Notificar en Telegram que la configuracion cambio
        enviar_telegram(
            f"⚙️ *Configuracion actualizada*\n"
            f"\n"
            f"₿ Umbral BTC: *${umbral_btc:,.2f}*\n"
            f"◎ Umbral SOL: *${umbral_sol:,.2f}*"
        )
        return

    # Mensaje de precio de Bitcoin
    if topico == "cripto/precios/btc":
        precio = float(mensaje)
        print(f"BTC: ${precio:,.2f}")
        # Analizar cambio y notificar en Telegram
        notificar_precio("BTC", precio, precio_ant_btc, umbral_btc)
        # Guardar precio actual como anterior para la proxima comparacion
        precio_ant_btc = precio

    # Mensaje de precio de Solana
    if topico == "cripto/precios/sol":
        precio = float(mensaje)
        print(f"SOL: ${precio:,.2f}")
        # Analizar cambio y notificar en Telegram
        notificar_precio("SOL", precio, precio_ant_sol, umbral_sol)
        # Guardar precio actual como anterior para la proxima comparacion
        precio_ant_sol = precio

# ── INICIAR CLIENTE MQTT ──────────────────────────────────────

# Crear cliente MQTT con la version 1 del API de callbacks
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)

# Asignar las funciones que se ejecutan al conectar y al recibir mensajes
client.on_connect = on_connect
client.on_message = on_message

print("Iniciando Monitor de Telegram...")
# Conectar al broker y mantener el programa corriendo indefinidamente
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()  # Mantiene el cliente escuchando mensajes continuamente
