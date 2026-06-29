using System.Collections.Generic;
using System.Linq;
using TMPro;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.Video;

public class CameraViewVideo : MonoBehaviour
{
    [Header("Configuration")] 
    public float normalFOV = 60f;
    public float rotationSpeed = 5f;
        
    public float TargetFOV { get; set; }
    public float RaycastDistance { get; set; }
    
    // -------------------------------------------------
    // Private state
    // -------------------------------------------------
    private VideoPlayer lastVideoPlayer;
    private VideoPlayer[] videoPlayers;

    private GameObject player; 
    private PlayerRotateWithInertia playerRotation;
    private Camera mainCamera;
    private AudioSource waterAudio;
    
    private RawImage crosshair;
    private IEnumerable<TMP_Text> batteryViews;

    void Start()
    {
        videoPlayers = FindObjectsByType<VideoPlayer>();
        
        mainCamera = gameObject.GetComponent<Camera>();
        
        player = GameObject.FindGameObjectWithTag("Player");
        playerRotation = player.GetComponent<PlayerRotateWithInertia>();
        
        crosshair = GameObject.FindGameObjectWithTag("Crosshair").GetComponent<RawImage>();
        batteryViews = GameObject.FindGameObjectsWithTag("Battery").Select(obj => obj.GetComponent<TMP_Text>());
        waterAudio = GameObject.FindGameObjectWithTag("Audio").GetComponent<AudioSource>();
    }

    void Update()
    {
        Vector3 forward = player.transform.forward;
        
        foreach (VideoPlayer vp in videoPlayers)
        {
            Vector3 rayOrigin = player.transform.position;
            rayOrigin.y = vp.transform.position.y;
            Ray ray = new(rayOrigin, forward);
            
            Debug.DrawRay(rayOrigin, forward * RaycastDistance);
            if (!Physics.Raycast(ray, out RaycastHit hitInfo, RaycastDistance))
            {
                continue;
            }

            // idk why tf vp isn't the same
            VideoPlayer videoPlayer = hitInfo.collider.GetComponent<VideoPlayer>();
            
            if (lastVideoPlayer != videoPlayer && playerRotation.Velocity == 0)
            {
                if (!videoPlayer.isPlaying)
                {
                    videoPlayer.Play();
                }

                lastVideoPlayer = videoPlayer;
            }
                
            if (playerRotation.Velocity == 0)
            {
                Quaternion current = mainCamera.transform.rotation;
                current.SetLookRotation(hitInfo.transform.position - mainCamera.transform.position);
                
                ViewVideo(true, current.normalized);
            }
            else
            {
                ViewVideo(false, Quaternion.identity);
            }

            return;
        }
        
        ViewVideo(false, Quaternion.identity);
            
        foreach (VideoPlayer vp in videoPlayers)
        {
            if (vp.isPlaying)
            {
                vp.Pause();
            }
        }
        
        lastVideoPlayer = null;
    }

    // -------------------------------------------------
    // Zoom and rotation helpers
    // -------------------------------------------------
    private void ViewVideo(bool viewing, Quaternion rotation)
    {
        float newZoom = viewing ? TargetFOV : normalFOV;
        mainCamera.fieldOfView = Mathf.Lerp(mainCamera.fieldOfView, newZoom, Time.deltaTime);

        float alpha = viewing ? 0 : 1;
        float lerpAlpha = Mathf.Lerp(crosshair.color.a, alpha, Time.deltaTime);
        
        Color crosshairColor = crosshair.color;
        crosshairColor.a = lerpAlpha;
        crosshair.color = crosshairColor;
        
        foreach (TMP_Text batteryView in batteryViews)
        {
            Color color = batteryView.color;
            color.a = lerpAlpha;
            batteryView.color = color;
        }

        float volume = viewing ? 0 : 0.4f ;
        waterAudio.volume = Mathf.Lerp(waterAudio.volume, volume, Time.deltaTime);

        if (viewing)
        {
            mainCamera.transform.rotation = Quaternion.Slerp(mainCamera.transform.rotation.normalized, 
                rotation, rotationSpeed * Time.deltaTime);
        }
        else
        {
            mainCamera.transform.localRotation = Quaternion.Slerp(mainCamera.transform.localRotation.normalized, 
                rotation, rotationSpeed * Time.deltaTime);
        }
    }
}