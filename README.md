# Intelligent Irrigation System with Chatbot Management

WIP

## Introduction
Water is a finite natural resource, with increasing scarcity. Agriculture accounts for approximately 70% of all freshwater consumption, and inefficient irrigation methods contribute significantly to water waste. This project seeks to minimize such waste through the automation of the irrigation process using sensor data and a smart decision-making system.

The present work addresses the development of an intelligent irrigation system aimed at optimizing water usage in agriculture. This project proposes the creation of a Wireless Sensor Network (WSN) that monitors soil moisture, the water reservoir level, and triggers automated irrigation. Additionally, it provides a user-friendly communication interface via a WhatsApp chatbot.

## System Architecture
- **WSN**: Consists of nodes that communicate via ESP-NOW. Node 1 monitors soil moisture, Node 2 measures the water reservoir level, Node 3 controls the solenoid valve for irrigation, and Node 4 acts as a gateway.
- **Backend**: Utilizes Oracle Express Edition (XE) for data storage and Oracle Rest Data Services (ORDS) to create a REST API, facilitating data management and interaction with the chatbot.
- **Chatbot**: Integrated into WhatsApp, it allows users to query the system's status and obtain data on soil moisture and irrigation history.

## Results and Discussion
WIP

## Future Improvements
- **Solar Power**: Transitioning nodes to solar power could enhance system sustainability and eliminate the need for electrical wiring.
- **Natural Language Processing**: Adding natural language processing capabilities to the chatbot could improve user interaction.
- **Circuit Integration**: Developing a custom circuit board to integrate components could reduce size, improve scalability, and prevent connection issues.

---
